# Procrastinator

for GGK_procrastinator_critical
use: [freq] [load] [core count] [C-state] [latency constraint] [(opt)DVFS latency]

there are several other features can be defined in the c file

define quiet output detail result(0: enable , 1: disable)

define debug output debug info(0: disable , 1: enable)

define printdist output pdf(0: disable , 1: enable)

define read_req_trace read request core,arrival time and service time from core (comment it out to disable)

define ivy use quad core ivy-bridge(comment it out to disable)

define critical only reschedule critical request only (comment it out to disable)

#Simulator design

The power consumption during the C-state transition is Pa(f)

The power consumption during the DVFS transition is (P(f_before)+P(f_after))/2

The during waking up , a core will experience an overhead of C-state transition, also, if the P-state is selected to be different from the P-state before sleep, a DVFS transition will also be added.

