reset
set xlabel 'size'
set ylabel 'time(ns)'
set title 'time measurement'
set term png enhanced font 'Verdana,10'
set output 'measure.png'

plot 'user_time.txt' using 1:2 with linespoints title 'user', \
'kernel_time.txt' using 1:2 with linespoints title 'kernel', \
'kernel_user_time.txt' using 1:2 with linespoints title 'kernel to user'
