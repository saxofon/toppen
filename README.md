# toppen
toppen is a small process load indicator, trying to cope with that it might have been starved (toppen itself that is) during measurement

Might be run like:

taskset -c 2 chrt -f 99 ./toppen $(pidof dummy-load) > toppen.log

and will then print out the cpu load that dummy-load put to the system,
once per second.
