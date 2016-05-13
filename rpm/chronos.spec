Name:           chronos
Version:        1.2
Release:        1
Summary:        Clearwater distributed timer store

License:        GPL
URL:            http://github.com/Metaswitch/chronos

%description
Clearwater distributed timer store

%install
%define _copy_to_buildroot() mkdir -p %{buildroot}%2; cp -r %{_topdir}/%1 %{buildroot}%2

# Copy the necessary files to the buildroot. 
#
# There are lots of ways to do this:
# - Manually write the necessary mkdir/cp commands
# - Use macros to make writing these commands easier
# - Call a helper script to populate the buildroot (e.g. by parsing debian/*.install)
#
# Here is an example of doing this with macros. 
%{_copy_to_buildroot build/bin/chronos /usr/bin/}
%{_copy_to_buildroot modules/cpp-common/scripts/stats-c/cw_stat /usr/share/clearwater/chronos/bin/}
%{_copy_to_buildroot usr/lib/*.so /usr/share/chronos/lib/}
%{_copy_to_buildroot usr/lib/*.so.* /usr/share/chronos/lib}
%{_copy_to_buildroot chronos.root/* /}

# Equivilent shell commands. 
#mkdir -p %{buildroot}/usr/bin
#cp %{_topdir}/build/bin/chronos %{buildroot}/usr/bin/
#mkdir -p %{buildroot}/usr/share/clearwater/chronos/bin
#cp %{_topdir}/modules/cpp-common/scripts/stats-c/cw_stat %{buildroot}/usr/share/clearwater/chronos/bin
#mkdir -p %{buildroot}/usr/share/chronos/lib
#cp %{_topdir}/usr/lib/*.so %{buildroot}/usr/share/chronos/lib
#cp %{_topdir}/usr/lib/*.so.* %{buildroot}/usr/share/chronos/lib
#cp -r %{_topdir}/chronos.root/* %{buildroot}/

%{_copy_to_buildroot debian/chronos.postinst /usr/share/clearwater/bin/}
%{_copy_to_buildroot debian/chronos.prerm /usr/share/clearwater/bin/}

mkdir %{buildroot}/etc/init.d/
cp %{_topdir}/debian/chronos.init.d %{buildroot}/etc/init.d/chronos

%debug_package

%post
/usr/share/clearwater/bin/chronos.postinst "configure"

%preun
case "$1" in
  0)
    # This is an un-installation.
    /usr/share/clearwater/bin/chronos.prerm "remove"
  ;;
  1)
    # This is an upgrade.
    /usr/share/clearwater/bin/chronos.prerm "upgrade"
  ;;
esac

%files
# Specify the files to package. 
#
# Another option is to use `%files -f <listfile>` where the contents of listfile is
# injected into the %files section
%config(noreplace) /etc/chronos/chronos.conf.sample
%config(noreplace) /etc/cron.hourly/chronos-log-cleanup

/usr/bin/chronos
/usr/share/chronos/chronos.monit
/usr/share/chronos/lib/libcares.so
/usr/share/chronos/lib/libcares.so.2
/usr/share/chronos/lib/libcares.so.2.1.0
/usr/share/chronos/lib/libcurl.so
/usr/share/chronos/lib/libcurl.so.4
/usr/share/chronos/lib/libcurl.so.4.3.0
/usr/share/chronos/lib/libzmq.so.5
/usr/share/chronos/write_monit_restart_diags
/usr/share/clearwater/bin/chronos_configuration_split.py
/usr/share/clearwater/bin/poll_chronos.sh
/usr/share/clearwater/chronos/bin/cw_stat
/usr/share/clearwater/clearwater-cluster-manager/plugins/chronos_plugin.py
/usr/share/clearwater/clearwater-cluster-manager/plugins/chronos_plugin.pyc
/usr/share/clearwater/clearwater-cluster-manager/plugins/chronos_plugin.pyo
/usr/share/clearwater/clearwater-diags-monitor/scripts/chronos_diags
/usr/share/clearwater/infrastructure/alarms/chronos_alarms.json
/usr/share/clearwater/infrastructure/scripts/check-chronos-uptime
/usr/share/clearwater/infrastructure/scripts/chronos

%attr(755, root, root) /usr/share/clearwater/bin/chronos.postinst
%attr(755, root, root) /usr/share/clearwater/bin/chronos.prerm
%attr(755, root, root) /etc/init.d/chronos
