printf 'target\n' > real.txt; ln -s real.txt link.txt
printf 'target2\n' > real2.txt; ln -s real2.txt link2.txt
printf 'target3\n' > real3.txt; ln -s real3.txt link3.txt
mkdir realdir; ln -s realdir linkdir; ln -s nowhere dangling
printf 'src\n' > src.txt
