require 'etc'
require 'fileutils'

module SvnTestUtil
  module Windows
    module Svnserve
      SERVICE_NAME = 'test-svn-server'

      class << self
        def escape_value(value)
          escaped_value = value.gsub(/"/, '\\"') # "
          "\"#{escaped_value}\""
        end
      end

      def service_control(command, args={})
        args = args.collect do |key, value|
          "#{key}= #{Svnserve.escape_value(value)}"
        end.join(" ")
        if `sc #{command} #{SERVICE_NAME} #{args}`.match(/FAILED/)
          raise "Failed to #{command} #{SERVICE_NAME}: #{args}"
        end
      end

      def grant_everyone_full_access(dir)
        dir = dir.tr(File::SEPARATOR, File::ALT_SEPARATOR)
        `cacls #{Svnserve.escape_value(dir)} /T /E /P Everyone:F`
      end

      def service_exists?
        begin
          service_control("query")
          true
        rescue
          false
        end
      end

      def setup_svnserve
        @svnserve_port = @svnserve_ports.first
        @repos_svnserve_uri = "svn://#{@svnserve_host}:#{@svnserve_port}"
        grant_everyone_full_access(@full_repos_path)

        unless service_exists?
          svnserve_dir = File.expand_path(File.join(@base_dir, "svnserve"))
          FileUtils.mkdir_p(svnserve_dir)
          at_exit do
            service_control('delete') if service_exists?
            FileUtils.rm_rf(svnserve_dir)
          end
          targets = %w(svnserve.exe libsvn_subr-1.dll libsvn_repos-1.dll
                       libsvn_fs-1.dll libsvn_delta-1.dll
                       libaprutil.dll libapr.dll sqlite3.dll)
          ENV["PATH"].split(";").each do |path|
            found_targets = []
            targets.each do |target|
              target_path = "#{path}\\#{target}"
              if File.exists?(target_path)
                found_targets << target
                FileUtils.cp(target_path, svnserve_dir)
              end
            end
            targets -= found_targets
            break if targets.empty?
          end
          unless targets.empty?
            raise "can't find libraries to work svnserve: #{targets.join(' ')}"
          end

          grant_everyone_full_access(svnserve_dir)

          svnserve_path = File.join(svnserve_dir, "svnserve.exe")
          svnserve_path = svnserve_path.tr(File::SEPARATOR,
                                           File::ALT_SEPARATOR)
          svnserve_path = Svnserve.escape_value(svnserve_path)

          root = @full_repos_path.tr(File::SEPARATOR, File::ALT_SEPARATOR)

          args = ["--service", "--root", Svnserve.escape_value(root),
                  "--listen-host", @svnserve_host,
                  "--listen-port", @svnserve_port]
          user = ENV["USERNAME"] || Etc.getlogin
          service_control('create',
                          [["binPath", "#{svnserve_path} #{args.join(' ')}"],
                           ["DisplayName", SERVICE_NAME],
                           ["type", "own"]])
        end
        service_control('stop') rescue nil
        service_control('start')
      end

      def teardown_svnserve
        service_control('stop') if service_exists?
      end

      def add_pre_revprop_change_hook
        File.open("#{@repos.pre_revprop_change_hook}.cmd", "w") do |hook|
          hook.print <<-HOOK
set REPOS=%1
set REV=%2
set USER=%3
set PROPNAME=%4
if "%PROPNAME%" == "#{Svn::Core::PROP_REVISION_LOG}" if "%USER%" == "#{@author}" exit 0
exit 1
          HOOK
        end
      end
    end

    module SetupEnvironment
      def setup_test_environment(top_dir, base_dir, ext_dir)
        build_type = "Release"

        FileUtils.mkdir_p(ext_dir)

        relative_base_dir =
          base_dir.sub(/^#{Regexp.escape(top_dir + File::SEPARATOR)}/, '')
        build_base_dir = File.join(top_dir, build_type, relative_base_dir)

        dll_dir = File.expand_path(build_base_dir)
        subversion_dir = File.join(build_base_dir, "..", "..", "..")
        subversion_dir = File.expand_path(subversion_dir)

        util_name = "util"
        build_conf = File.join(top_dir, "build.conf")
        File.open(File.join(ext_dir, "#{util_name}.rb" ), 'w') do |util|
          setup_dll_wrapper_util(dll_dir, util)
          add_depended_dll_path_to_dll_wrapper_util(top_dir, build_type, util)
          add_svn_dll_path_to_dll_wrapper_util(build_conf, subversion_dir, util)
          setup_dll_wrappers(build_conf, ext_dir, dll_dir, util_name) do |lib|
            svn_lib_dir = File.join(subversion_dir, "libsvn_#{lib}")
            util.puts("add_path.call(#{svn_lib_dir.dump})")
          end

          svnserve_dir = File.join(subversion_dir, "svnserve")
          util.puts("add_path.call(#{svnserve_dir.dump})")
        end
      end

      private
      def setup_dll_wrapper_util(dll_dir, util)
        libsvn_swig_ruby_dll_dir = File.join(dll_dir, "libsvn_swig_ruby")

        util.puts(<<-EOC)
paths = ENV["PATH"].split(';')
add_path = Proc.new do |path|
  win_path = path.tr(File::SEPARATOR, File::ALT_SEPARATOR)
  unless paths.include?(win_path)
    ENV["PATH"] = "\#{win_path};\#{ENV['PATH']}"
  end
end

add_path.call(#{dll_dir.dump})
add_path.call(#{libsvn_swig_ruby_dll_dir.dump})
EOC
      end

      def add_depended_dll_path_to_dll_wrapper_util(top_dir, build_type, util)
        lines = []
        gen_make_opts = File.join(top_dir, "gen-make.opts")
        lines = File.read(gen_make_opts).to_a if File.exists?(gen_make_opts)
        config = {}
        lines.each do |line|
          name, value = line.chomp.split(/\s*=\s*/, 2)
          config[name] = value if value
        end

        [
         ["apr", build_type],
         ["apr-util", build_type],
         ["apr-iconv", build_type],
         ["berkeley-db", "bin"],
         ["sqlite", "bin"],
        ].each do |lib, sub_dir|
          lib_dir = config["--with-#{lib}"] || lib
          dirs = [top_dir, lib_dir, sub_dir].compact
          dll_dir = File.expand_path(File.join(*dirs))
          util.puts("add_path.call(#{dll_dir.dump})")
        end
      end

      def add_svn_dll_path_to_dll_wrapper_util(build_conf, subversion_dir, util)
        File.open(build_conf) do |f|
          f.each do |line|
            if /^\[(libsvn_.+)\]\s*$/ =~ line
              lib_name = $1
              lib_dir = File.join(subversion_dir, lib_name)
              util.puts("add_path.call(#{lib_dir.dump})")
            end
          end
        end
      end

      def setup_dll_wrappers(build_conf, ext_dir, dll_dir, util_name)
        File.open(build_conf) do |f|
          f.each do |line|
            if /^\[swig_(.+)\]\s*$/ =~ line
              lib_name = $1
              File.open(File.join(ext_dir, "#{lib_name}.rb" ), 'w') do |rb|
                rb.puts(<<-EOC)
require File.join(File.dirname(__FILE__), #{util_name.dump})
require File.join(#{dll_dir.dump}, File.basename(__FILE__, '.rb')) + '.so'
EOC
              end

              yield(lib_name)
            end
          end
        end
      end
    end
  end
end
