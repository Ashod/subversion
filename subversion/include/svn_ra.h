/*
 * svn_ra.h :  structures related to repository access
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_RA_H
#define SVN_RA_H

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_dso.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_wc.h"

/*** RA: TNG ***/

#if 0
/* A function type for "cleaning up" after a commit.  The client layer
   supplies this routine to an RA layer.  RA calls this routine on
   each PATH that was committed, allowing the client to bump revision
   numbers. */
typedef svn_error_t *svn_ra_close_commit_func_t (void *close_baton,
                                                 svn_string_t *path,
                                                 svn_revnum_t new_rev);


/* A function type which allows the RA layer to store WC properties
   after a commit.  */
typedef svn_error_t *svn_ra_set_wc_prop_func_t (void *close_baton,
                                                svn_string_t *path,
                                                svn_string_t *name,
                                                svn_string_t *value);


/* A vtable structure which allows a working copy to describe a
   subset (or possibly all) of its working-copy to an RA layer. */
  
typedef struct svn_ra_reporter_t
{
  /* Given a ROOT_PATH within a working copy, describe the REVISION
     and REPOSITORY_PATH it corresponds to.   (ROOT_PATH will be where
     the WC update begins.) */
  svn_error_t *(*set_baseline) (void *report_baton,
                                svn_revnum_t revision,
                                svn_string_t *repository_path,
                                svn_string_t *root_path);

  /* Describe an entire subtree DIR_PATH as being at a particular
     REVISION; this will *override* any previous set_directory() calls
     made on DIR_PATH's parents.  DIR_PATH is relative to the ROOT_PATH
     specified in set_baseline(). */
  svn_error_t *(*set_directory) (void *report_baton,
                                 svn_string_t *dir_path,
                                 svn_revnum_t revision);
  
  /* Describe a file FILE_PATH as being at a particular REVISION; this
     will *override* any previous set_file() calls made on FILE_PATH's
     parents.  FILE_PATH is relative to the ROOT_PATH specified
     in set_baseline(). */
  svn_error_t *(*set_file) (void *report_baton,
                            svn_string_t *file_path,
                            svn_revnum_t revision);
  
  /* WC calls this when the state report is finished; any directories
     or files not explicitly `set' above are assumed to be at the
     baseline revision.  */
  svn_error_t *(*finish_report) (void *report_baton);

} svn_ra_reporter_t;




  /* A vtable structure which encapsulates all the functionality of a
     particular repository-access implementation.
     
     Note: libsvn_client will keep an array of these objects,
     representing all RA libraries that it has simultaneously loaded
     into memory.  Depending on the situation, the client can look
     through this array and find the appropriate implementation it
     needs. */

typedef struct svn_ra_plugin_t
{
  const char *name;         /* The name of the ra library,
                                 e.g. "ra_dav" or "ra_local" */

  const char *description;  /* Short documentation string */

  /* The vtable hooks */
  
  /* Open a "session" with a repository at URL.  *SESSION_BATON is
     returned and then used (opaquely) for all further interactions
     with the repository. */
  svn_error_t *(*open) (void **session_baton,
                        svn_string_t *repository_URL,
                        apr_pool_t *pool);


  /* Close a repository session. */
  svn_error_t *(*close) (void *session_baton);

  /* Get the latest revision number from the repository. This is
     usefule for the `svn status' command.  :) */
  svn_error_t *(*get_latest_revnum) (void *session_baton,
                                     svn_revnum_t *latest_revnum);


  /* Begin a commit, using LOG_MSG.  RA returns an *EDITOR and
     *EDIT_BATON capable of transmitting a commit to the repository,
     which is then driven by the client.

     RA must guarantee:
     
          1. That it will track each item that is committed
          2. That close_edit() will "finish" the commit by calling
             CLOSE_FUNC (with CLOSE_BATON) on each item that was
             committed.  

     Optionally, the RA layer may also call SET_FUNC to store WC
     properties on committed items.  */
  svn_error_t *(*get_commit_editor) (void *session_baton,
                                     const svn_delta_edit_fns_t **editor,
                                     void **edit_baton,
                                     svn_string_t *log_msg,
                                     svn_ra_close_commit_func_t close_func,
                                     svn_ra_set_wc_prop_func_t set_func,
                                     void *close_baton);


  /* Ask the network layer to check out a copy of ROOT_PATH from the
     repository's filesystem, using EDITOR and EDIT_BATON to create a
     working copy. */
  svn_error_t *(*do_checkout) (void *session_baton,
                               svn_string_t *root_path,
                               const svn_delta_edit_fns_t *editor,
                               void *edit_baton);


  /* Ask the network layer to update part (or all) of a working copy.

     The client initially provides an UPDATE_EDITOR and UPDATE_BATON
     to the RA layer, and receives a REPORTER structure and
     REPORT_BATON in return.

     The client describes its working-copy revision numbers (of items
     relevant to TARGETS only!) by making calls into the REPORTER
     structure, starting at the top-most directory it wishes to be
     updated.  When finished, it calls REPORTER->finish_report().

     The RA layer then uses UPDATE_EDITOR to update each target in
     TARGETS.  */
  svn_error_t *(*do_update) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             apr_array_header_t *targets,
                             const svn_delta_edit_fns_t *update_editor,
                             void *update_baton);

} svn_ra_plugin_t;

#endif  /*** RA: TNG ***/



/* --------------------------------------------------------------------*/


/* A vtable structure that encapsulates all the functionality of a
   particular repository-access implementation.

   Note: libsvn_client will keep an array of these objects,
   representing all RA libraries that it has simultaneously loaded
   into memory.  Depending on the situation, the client can look
   through this array and find the appropriate implementation it
   needs. */

typedef struct svn_ra_plugin_t
{
  const char *name;         /* The name of the ra library,
                                 e.g. "ra_dav" or "ra_local" */

  const char *description;  /* Short documentation string */

  /* The vtable hooks */
  
  /* Open a "session" with a repository at URL.  *SESSION_BATON is
     returned and then used (opaquely) for all further interactions
     with the repository. */
  svn_error_t *(*open) (void **session_baton,
                        svn_string_t *repository_URL,
                        apr_pool_t *pool);


  /* Close a repository session. */
  svn_error_t *(*close) (void *session_baton);

  /* Get the latest revision number from the repository. */
  svn_error_t *(*get_latest_revnum) (void *session_baton,
                                     svn_revnum_t *latest_revnum);


  /* Return an *EDITOR and *EDIT_BATON capable of transmitting a
     commit to the repository beginning at absolute repository path
     ROOT_PATH.  Also, ra's editor must guarantee that if close_edit()
     returns successfully, that *NEW_REVISION will be set to the
     repository's new revision number resulting from the commit. */
  svn_error_t *(*get_commit_editor) (void *session_baton,
                                     const svn_delta_edit_fns_t **editor,
                                     void **edit_baton,
                                     svn_revnum_t *new_revision);


  /* Ask the network layer to check out a copy of ROOT_PATH from a
     repository's filesystem, using EDITOR and EDIT_BATON to create a
     working copy. */
  svn_error_t *(*do_checkout) (void *session_baton,
                               const svn_delta_edit_fns_t *editor,
                               void *edit_baton);


  /* Ask the network layer to update a working copy from URL.

     The client library provides a ROOT_PATH (absolute path within the
     repository) that represents to the topmost working copy subdir
     that will be updated.  The client also provides an UPDATE_EDITOR
     (and baton) that can be used to modify the working copy.

     The network layer then returns a REPORT_EDITOR and REPORT_BATON
     to the client; the client first uses this to transmit an empty
     tree-delta to the repository which describes all revision numbers
     in the working copy.

     There is one special property of the REPORT_EDITOR: its
     close_edit() function.  When the client calls close_edit(), the
     network layer then talks to the repository and proceeds to use
     UPDATE_EDITOR and UPDATE_BATON to patch the working copy.

     When the update_editor->close_edit() returns, then
     report_editor->close_edit() returns too.  Therefore the return
     value of report_editor->close_edit() contains the result of the
     entire update.  */
  svn_error_t *(*do_update) (void *session_baton,
                             const svn_delta_edit_fns_t **report_editor,
                             void **report_baton,
                             const svn_delta_edit_fns_t *update_editor,
                             void *update_baton);

} svn_ra_plugin_t;





/* svn_ra_init_func_t :
   
   libsvn_client will be reponsible for loading each RA DSO it needs.
   However, all "ra_FOO" implementations *must* export a function named
   `svn_ra_FOO_init()' of type `svn_ra_init_func_t'.

   When called by libsvn_client, this routine simply returns an
   internal, static plugin structure.  POOL is a pool for allocating
   configuration / one-time data.  */
typedef svn_error_t *svn_ra_init_func_t (int abi_version,
                                         apr_pool_t *pool,
                                         const svn_ra_plugin_t **plugin);



#endif  /* SVN_RA_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
