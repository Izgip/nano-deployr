# GNU nano-deployr
This is the official Github repo for Nano Deployer. It is currently under the LGPL v3 License. Please adapt your use of this software accordingly.


# Why use nano-deployr?
Nano Deployer (or Nanod for short) is a lightweight tool allowing to distribute quickly software in a single file.
Run a file and then it extracts all the files which should have been included in the file's project's Nanodfile where the Nanodfile specifies they should go.

# Why can't i just use python scripts or bash scripts?
Nano Deployer is designed to automatically compress files with zlib upon building a Nanod extracter.
It also has the benefit of being native and thus does not depend on python3 or any other interpreted language.

# How can i know this isn't ratted or anything like this?
Nano Deployer is fully open-source and properly documented with clean comments (triple-checked for errors) and indent.
It also can be forked and modified to use other compression algorithms (LZ4, zstandard, etc..) but the credits must go to the original author, that is, Yanayer/@Izgip.

