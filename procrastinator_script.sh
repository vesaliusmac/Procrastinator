for n in {0..7..1};
do
	load_step=0.1;
	load=0.1;
	loadbound=1.0;
	printf 'schedule_policy %d\n' $n >> configuration;
	printf 'DVFS_latency 100\n' >> configuration;
	while [ "$(echo "${load} < ${loadbound}" | bc)" -eq 1 ];
	do
		./GGk_procrastinator_central_critical 0 $load 12 3 $1;
		load=$(echo "$load+$load_step" | bc);
	done
	
	# echo "schedule_policy " > test;
	# echo 2 > test;
	# echo -e > test;
	# cat test;
	rm configuration;

done