A20_IP="192.168.1.117"
PASS="adatis"
FILE=update.tar.gz
FILE1=update1.tar.gz

tar cjf $FILE boot
cd lib
tar cjf ../$FILE1 modules
cd ..

#ssh -oStrictHostKeyChecking=no root@$A20_IP uptime
ssh-keygen -R $A20_IP
sshpass -p $PASS ssh -o StrictHostKeyChecking=no root@$A20_IP uptime

sshpass -p $PASS scp ./$FILE root@$A20_IP:/root
sshpass -p $PASS scp ./$FILE1 root@$A20_IP:/root


sshpass -p $PASS ssh root@$A20_IP "tar xf ${FILE} -C /"
sshpass -p $PASS ssh root@$A20_IP "tar xf ${FILE1} -C /lib"
sshpass -p $PASS ssh root@$A20_IP 'sync'
sshpass -p $PASS ssh root@$A20_IP 'reboot'

echo Done