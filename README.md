# random_run
run a command with randomized list of arguments (C++17)

read the manpage for details

Basically, this is a toy project to learn some features of C++17. 

It proves to be surprisingly useful to me, I have some large collections 
of files that I occasionally want to peruse
(videos / music) in a random order, but the tools I use are not too 
good for that (between the large lists and the spaces in some names).

For instance, mpv shuffle will replay the same file several times.

Occasionally, I also want to filter files, e.g., exclude pdf files from mpv perview.

Another useful instance is feh. feh has a random mode, but you don't know what it actually displays.
So if you want to remove a picture from a large set of backgrounds, rr comes to the rescue:
instead of something like 

feh -Z --bg-center -z backgrounds/

you can use something like 

rr -1vr feh -Z --bg-center -- backgrounds/

and presto! you've got your image name.

Also works great with xwallpaper in a hurry.
