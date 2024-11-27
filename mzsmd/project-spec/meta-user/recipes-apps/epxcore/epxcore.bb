SUMMARY = "epxcore autoconf tools and libraries"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

#SRCREV = "master"
#SRC_URI = "git:///home/berta/work/final-project/epxcore/"

#S = "${WORKDIR}/git"
SRC_URI = "file://epxcore \
        "
S = "${WORKDIR}/epxcore"
CFLAGS:prepend = "-I ${S}/include"
inherit autotools

PARALLEL_MAKE = ""

FILES_${PN} += "/etc/init.d/epxcore"

do_install:append() {
#	install -d ${D}/opt/epx/bin

    install -d ${D}${sysconfdir}/init.d
    install -d ${D}${sysconfdir}/rcS.d
    install -d ${D}${sysconfdir}/rc1.d
    install -d ${D}${sysconfdir}/rc2.d
    install -d ${D}${sysconfdir}/rc3.d
    install -d ${D}${sysconfdir}/rc4.d
    install -d ${D}${sysconfdir}/rc5.d
    
#    install -m 0755 ${S}/src/epxcore  ${D}/opt/epx/bin/

	install -m 0755 ${S}/extra/epxcore  ${D}/etc/init.d/
    
	ln -sf ../init.d/epxcore  ${D}${sysconfdir}/rc1.d/S99epxcore
	ln -sf ../init.d/epxcore  ${D}${sysconfdir}/rc2.d/S99epxcore
	ln -sf ../init.d/epxcore  ${D}${sysconfdir}/rc3.d/S99epxcore
	ln -sf ../init.d/epxcore  ${D}${sysconfdir}/rc4.d/S99epxcore
	ln -sf ../init.d/epxcore  ${D}${sysconfdir}/rc5.d/S99epxcore

}
