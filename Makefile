all: clean GGk_dyn_sleep GGk_dreamweaver GGk_powernap GGk_rubik GGk_dyn_sleep_dvfs GGk_procrastinator_central GGk_procrastinator_central_critical GGk_sleepscale GGk_default

build: GGk_dyn_sleep GGk_dreamweaver GGk_powernap GGk_rubik GGk_dyn_sleep_dvfs GGk_procrastinator_central GGk_procrastinator_central_critical GGk_sleepscale GGk_default

GGk_dyn_sleep: GGk_dyn_sleep.c arrival.h
	gcc -std=c99 -o GGk_dyn_sleep GGk_dyn_sleep.c -lm
GGk_dreamweaver: GGk_dreamweaver.c arrival.h
	gcc -std=c99 -o GGk_dreamweaver GGk_dreamweaver.c -lm
GGk_powernap: GGk_powernap.c arrival.h
	gcc -std=c99 -o GGk_powernap GGk_powernap.c -lm
GGk_rubik: GGk_rubik.c arrival.h
	gcc -std=c99 -o GGk_rubik GGk_rubik.c -lm
GGk_dyn_sleep_dvfs: GGk_dyn_sleep_dvfs.c arrival.h
	gcc -std=c99 -o GGk_dyn_sleep_dvfs GGk_dyn_sleep_dvfs.c -lm
GGk_procrastinator_central: GGk_procrastinator_central.c arrival.h
	gcc -std=c99 -o GGk_procrastinator_central GGk_procrastinator_central.c -lm
GGk_procrastinator_central_critical: GGk_procrastinator_central_critical.c arrival.h
	gcc -std=c99 -o GGk_procrastinator_central_critical GGk_procrastinator_central_critical.c -lm
GGk_sleepscale: sleepscale_bruteforce.c GGk_sleepscale.c arrival.h
	gcc -std=c99 -o sleepscale_per_config sleepscale_per_config.c -lm
	gcc -std=gnu99 -o GGk_sleepscale GGk_sleepscale.c -lm
GGk_default: GGk_default.c arrival.h
	gcc -std=c99 -o GGk_default GGk_default.c -lm
clean:
	rm -f GGk_dyn_sleep
	rm -f GGk_powernap
	rm -f GGk_dreamweaver
	rm -f GGk_rubik
	rm -f GGk_dyn_sleep_dvfs
	rm -f GGk_procrastinator_central
	rm -f GGk_procrastinator_central_critical
	rm -f sleepscale_per_config
	rm -f GGk_sleepscale
	rm -f GGk_default
