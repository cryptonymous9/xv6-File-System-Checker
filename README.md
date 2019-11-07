CS 301: Operating Systems Course Project
# XV6 File System Checker [Under Development]

## Description
A File System Checker, which reads in a file system image and makes sure that it is consistent. We are using XV6 image for testing. 

## Usage

```
git clone https://github.com/mit-pdos/xv6-public.git 
git clone https://github.com/cryptonymous9/xv6-File-System-Checker.git
cp xv6-File-System-Checker/*  xv6-public/
cd xv6-public/
gcc -o check_fs check_fs.c
./check_fs e[error number].img
```
## References
Gantt Charts made using https://app.instagantt.com/r#
https://medium.com/@ppan.brian/file-systems-in-xv6-8603fdd33dd6
MIT xv6 book: https://pdos.csail.mit.edu/6.828/2012/xv6/book-rev7.pdf

--
