SUMMARY = "Recipe for  build an external epsmdrv Linux kernel module"
SECTION = "PETALINUX/modules"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit module

INHIBIT_PACKAGE_STRIP = "1"

SRC_URI = "file://Makefile \
           file://epsmdrv.c \
	   file://COPYING \
          "

S = "${WORKDIR}"

KERNEL_MODULE_AUTOLOAD += "epsmdrv"

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.
