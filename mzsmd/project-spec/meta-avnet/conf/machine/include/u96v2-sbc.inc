#@TYPE: Machine
#@NAME: u96v2-sbc
#@DESCRIPTION: Generic support for Ultra96v2 SBC.
#

MACHINEOVERRIDES =. "ultra96:ultra96-zynqmp:u96v2-sbc:"

require conf/machine/include/soc-zynqmp.inc
require conf/machine/include/machine-xilinx-default.inc

MACHINE_FEATURES += "rtc ext2 ext3 vfat usbhost usbgadget wifi bluetooth"

UBOOT_MACHINE = "xilinx_zynqmp_virt_defconfig"
# prevent U-Boot from deploying the boot.bin
SPL_BINARY = ""

SERIAL_CONSOLES ?= "115200;ttyPS0"

PMU_FIRMWARE_IMAGE_NAME ?= "pmu-firmware-zynqmp-pmu"
PMU_FIRMWARE_DEPLOY_DIR ?= "${TOPDIR}/pmutmp/deploy/images/zynqmp-pmu"

EXTRA_IMAGEDEPENDS += " \
		arm-trusted-firmware \
		virtual/bootloader \
		u-boot-xlnx-scr \
		virtual/fsbl \
		virtual/pmu-firmware \
		avnet-boot-scr \
		"

# Used to create a wic image to boot from sd
IMAGE_BOOT_FILES = " \
                    boot.bin \
                    boot.scr \
                    fitImage;image.ub \
                    "

WKS_FILES = "petalinux-image-minimal.wks"

# deploy avnet-boot-scr scripts in images/linux/ folder
EXTRA_FILESLIST:append = " ${DEPLOY_DIR_IMAGE}/avnet-boot/:avnet-boot/ "

MACHINE_ESSENTIAL_EXTRA_RRECOMMENDS += "linux-firmware-wl18xx"

EXTRA_IMAGEDEPENDS:remove = "qemu-devicetrees"

DISTRO_FEATURES:append = " bluez5 dbus"

PACKAGE_FEED_URIS = "http://petalinux.xilinx.com/sswreleases/sswboardfeeds/"
PACKAGE_FEED_BASE_PATHS = "zzsoc"
PACKAGE_FEED_ARCHS = "all aarch64 plnx_aarch64"
