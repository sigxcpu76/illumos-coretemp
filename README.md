A coretemp driver implementation for Illumos
============================================

This is a kstat producer. Included is an utility (```coretempstat```) that displays the temperatures in a nice format.

Install
-------

```make install```

Test
----

Sample output:

```console
# kstat -m cpu_info -n "coretemp*"
module: cpu_info                        instance: 0
name:   coretemp0                       class:    misc
        chip_id                         0
        chip_temp                       41
        core_id                         0
        core_temp                       33
        crtime                          258514.520731534
        snaptime                        258745.631569397
        target_temp                     90
        tj_max                          94
        valid                           1

module: cpu_info                        instance: 1
name:   coretemp1                       class:    misc
        chip_id                         0
        chip_temp                       41
        core_id                         1
        core_temp                       33
        crtime                          258514.520737457
        snaptime                        258745.631591757
        target_temp                     90
        tj_max                          94
        valid                           1
```

```coretempstat``` output:

```console
Found 8 CPUs in 1 socket
Socket #0 temp : 41 °C
        Core #0 temp : 32 °C
        Core #1 temp : 35 °C
        Core #2 temp : 33 °C
        Core #3 temp : 41 °C
```
