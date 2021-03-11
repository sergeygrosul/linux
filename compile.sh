make clean
make ARCH=arm adatis_cc_defconfig
##make ARCH=arm menuconfig #add bluethooth, etc.
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j16
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j16 INSTALL_MOD_PATH=out modules
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j16 INSTALL_MOD_PATH=out modules_install
cp ./arch/arm/boot/dts/sun8i-s3-adatis-cc.dtb ./out/boot/dtb
cp ./arch/arm/boot/dts/sun8i-v3s-adatis-cc.dtb ./out/boot/dtb
cp ./arch/arm/boot/zImage ./out/boot
rm ./out/lib/modules/5.2.0-licheepi-zero+/build
rm ./out/lib/modules/5.2.0-licheepi-zero+/source

