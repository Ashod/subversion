#!/usr/bin/env python

# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
# ====================================================================
"""\
Usage:  1. {PROGRAM} [OPTIONS] include INCLUDE-PATH ...
        2. {PROGRAM} [OPTIONS] exclude EXCLUDE-PATH ...

Read a Subversion revision log output stream from stdin, analyzing its
revision log history to see what paths would need to be additionally
provided as part of the list of included/excluded paths if trying to
use Subversion's 'svndumpfilter' program to include/exclude paths from
a full dump of a repository's history.

The revision log stream should be the result of 'svn log -v' or 'svn
log -vq' when run against the root of the repository whose history
will be filtered by a user with universal read access to the
repository's data.  Do not use the --use-merge-history (-g) or
--stop-on-copy when generating this revision log stream.

Options:

   --help (-h)           Show this usage message and exit.
   
   --verbose (-v)        Provide more information.  May be used multiple
                         times for additional levels of information (-vv).
"""
import sys
import os
import getopt
import string

verbosity = 0

class LogStreamError(Exception): pass
class EOFError(Exception): pass
  
def sanitize_path(path):
  return '/'.join(filter(None, path.split('/')))

def subsumes(path, maybe_child):
  if path == maybe_child:
    return True
  if maybe_child.find(path + '/') == 0:
    return True
  return False

def compare_paths(path1, path2):
  # Are the paths exactly the same?
  if path1 == path2:
    return 0

  # Skip past common prefix
  path1_len = len(path1);
  path2_len = len(path2);
  min_len = min(path1_len, path2_len)
  i = 0
  while (i < min_len) and (path1[i] == path2[i]):
    i = i + 1

  # Children of paths are greater than their parents, but less than
  # greater siblings of their parents
  char1 = '\0'
  char2 = '\0'
  if (i < path1_len):
    char1 = path1[i]
  if (i < path2_len):
    char2 = path2[i]

  if (char1 == '/') and (i == path2_len):
    return 1
  if (char2 == '/') and (i == path1_len):
    return -1
  if (i < path1_len) and (char1 == '/'):
    return -1
  if (i < path2_len) and (char2 == '/'):
    return 1

  # Common prefix was skipped above, next character is compared to
  # determine order
  return cmp(char1, char2)

def log(msg, min_verbosity):
  if verbosity >= min_verbosity:
    if min_verbosity == 1:
      sys.stderr.write("[* ] ")
    elif min_verbosity == 2:
      sys.stderr.write("[**] ")
    sys.stderr.write(msg + "\n")
    
class DependencyTracker:
  def __init__(self, include_paths):
    self.include_paths = include_paths[:]
    self.dependent_paths = []
    
  def path_included(self, path):
    for include_path in self.include_paths + self.dependent_paths:
      if subsumes(include_path, path):
        return True
    return False

  def handle_changes(self, path_copies):
    for path, copyfrom_path in path_copies.items():
      if self.path_included(path) and copyfrom_path:
        if not self.path_included(copyfrom_path):
          self.dependent_paths.append(copyfrom_path)

def readline(stream):
  line = stream.readline()
  if not line:
    raise EOFError("Unexpected end of stream")
  line = line.rstrip('\n\r')
  log(line, 2)
  return line

def svn_log_stream_get_dependencies(stream, included_paths):
  import re

  dt = DependencyTracker(included_paths)  

  header_re = re.compile(r'^r([0-9]+) \|.*$')
  action_re = re.compile(r'^   [ADMR] /(.*)$')
  copy_action_re = re.compile(r'^   [AR] /(.*) \(from /(.*):[0-9]+\)$')
  line_buf = None
  last_revision = 0
  
  while 1:
    try:
      line = line_buf is not None and line_buf or readline(stream)
    except EOFError:
      break
    if line != '-' * 72:
      raise LogStreamError("Expected log divider line; not found.")

    try:
      line = readline(stream)
    except EOFError:
      break
    match = header_re.search(line)
    if not match:
      raise LogStreamError("Expected log header line; not found.")

    pieces = map(string.strip, line.split('|'))
    revision = int(pieces[0][1:])
    if last_revision and revision >= last_revision:
      raise LogStreamError("Revisions are misordered.  Make sure log stream "
                           "is from 'svn log' with the youngest revisions "
                           "before the oldest ones (the default ordering).")
    log("Parsing revision %d" % (revision), 1)
    last_revision = revision
    idx = pieces[-1].find(' line')
    if idx != -1:
      log_lines = int(pieces[-1][:idx])
    else:
      log_lines = 0

    line = readline(stream)
    if line != 'Changed paths:':
      raise LogStreamError("Expected 'Changed paths:' line; not found.  Make "
                           "sure log stream is from 'svn log' with the "
                           "--verbose (-v) option.")

    path_copies = {}
    while 1:
      try:
        line = readline(stream)
      except EOFError:
        break
      match = action_re.search(line)
      if match:
        match = copy_action_re.search(line)
        if match:
          path_copies[sanitize_path(match.group(1))] = sanitize_path(match.group(2))
      else:
        dt.handle_changes(path_copies)
        if log_lines:
          for i in range(log_lines):
            readline(stream)
          line_buf = None
        else:
          line_buf = line
        break

  return dt

def analyze_logs(included_paths):
  print "Initial include paths:"
  for path in included_paths:
    print "   /%s" % (path)

  dt = svn_log_stream_get_dependencies(sys.stdin, included_paths)

  if dt.dependent_paths:
    print "Dependent include paths found:"
    for path in dt.dependent_paths:
      print "   /%s" % (path)
    print "You need to also include them (or one of their parents)."
  else:
    print "No new dependencies found!  You might still need " \
          "to manually create parent directories for the " \
          "included paths before loading a filtered dump:"
    parents = {}
    for path in dt.include_paths:
      while 1:
        parent = os.path.dirname(path)
        if not parent:
          break
        parents[parent] = 1
        path = parent
    parents = parents.keys()
    parents.sort(compare_paths)
    for parent in parents:
      print "   /%s" % (parent)

def usage_and_exit(errmsg=None):
  program = os.path.basename(sys.argv[0])
  stream = errmsg and sys.stderr or sys.stdout
  stream.write(__doc__.replace("{PROGRAM}", program))
  if errmsg:
    stream.write("\nERROR: %s\n" % (errmsg))
  sys.exit(errmsg and 1 or 0)

def main():
  config_dir = None
  
  try:
    opts, args = getopt.getopt(sys.argv[1:], "hv",
                               ["help", "verbose"])
  except getopt.GetoptError, e:
    usage_and_exit(str(e))
    
  for option, value in opts:
    if option in ['-h', '--help']:
      usage_and_exit()
    elif option in ['-v', '--verbose']:
      global verbosity
      verbosity = verbosity + 1

  if len(args) < 2:
    usage_and_exit("Not enough arguments")

  try:
    if args[0] == 'include':
      analyze_logs(map(sanitize_path, args[1:]))
    elif args[0] == 'exclude':
      usage_and_exit("Feature not implemented")
    else:
      usage_and_exit("Valid subcommands are 'include' and 'exclude'")
  except (LogStreamError, EOFError), e:
    log("ERROR: " + str(e), 0)
    sys.exit(1)

if __name__ == "__main__":
    main()