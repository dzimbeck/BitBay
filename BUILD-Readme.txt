If any of the patches don't work, then you can simply read the emails
and apply the patch manually by adding and removing the lines they
recommend in each file.

If leveldb fails, just use the files in the included folders for your platform
to replace the built ones in the src/leveldb directory

These scripts were tested on the following OS below, they can and should
work on many other OS. If you are running a different OS, simply remove
the check for it in the SH file.

== Prerequisite ==

needed OS:

win32: Debian 8 (jessie) x86_64 (VM or chroot)

linux32: Debian 7 (wheezy) i386 (VM or chroot)

linux64: Debian 7 (wheezy) x86_64 (VM or chroot)

osx32: OS X 10.6 (i386)
    - install Xcode from OS X ISO
    - install MacPorts from Web
  
osx64: OS X 10.11 (x86_64)
    - install Xcode (from AppStore or Web)
    - run Xcode and accept license
    - install Xcode command tools
    - install MacPorts from Web   

common:
1. install git and sudo
    OS X:
    # sudo port install git
    Debian:
    # su
    # apt-get -y install git sudo
    
2. allow sudo without password:
    OS X:
    # echo "$(whoami) ALL=(ALL) NOPASSWD: ALL" | sudo tee -a /etc/sudoers
    Debian:
    # su
    # echo "$(whoami) ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

== Build ==

# cd ~
# git clone http://hostname/git/blackcoin/
# ~/blackcoin/<RELEASE_NAME>.sh --reset --build
