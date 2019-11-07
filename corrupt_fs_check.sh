#!/bin/sh
echo Enter the Error number by which you want to corrupt the File System - [ 1 2 3 4 5 6 9 ]
read error
./mkfs fs.img
./corrupt_fs fs.img $error
echo "-----------------"
echo File system Corrupted with Error $error
echo " "
echo Running File System Checker
echo " "
./check_fs fs.img
echo " "