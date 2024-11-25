SUMMARY = "epxcore autoconf application"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

prefix="/opt/epx"
exec_prefix="/opt/epx"

SRCREV = "master"
#SRC_URI = "file://epxcore \
#        "
SRC_URI = "git:///home/berta/work/final-project/final-project-repository-bertac64/epxcore/"

S = "${WORKDIR}/git"
#S = "${WORKDIR}/epxcore"
#CFLAGS:prepend = "-I ${S}/include"

inherit autotools

PARALLEL_MAKE = ""

FILES_${PN} += /etc/init.d/epxcore"

do_install_append() {
	install -d ${D}${sysconfdir}/init.d
	install -d ${D}${sysconfdir}/rcS.d
	install -d ${D}${sysconfdir}/rc1.d
	install -d ${D}${sysconfdir}/rc2.d
	install -d ${D}${sysconfdir}/rc3.d
	install -d ${D}${sysconfdir}/rc4.d
	install -d ${D}${sysconfdir}/rc5.d
	
	install -m 0755 ${S}/extra/epxcore ${D}/etc/init.d/
	
	ln -sf ./epxcore ${D}${sysconfdir}/rc1.d/S99epxcore
	ln -sf ./epxcore ${D}${sysconfdir}/rc2.d/S99epxcore
	ln -sf ./epxcore ${D}${sysconfdir}/rc3.d/S99epxcore
	ln -sf ./epxcore ${D}${sysconfdir}/rc4.d/S99epxcore
	ln -sf ./epxcore ${D}${sysconfdir}/rc5.d/S99epxcore
}
