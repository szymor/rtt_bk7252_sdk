Example Description

This example describes how to use broadcast wakeup api in tickless sleepCG.

Requirement Components:
    tool that can send broadcast packet.You should set the udp port as the dst port 

Note:
	you should call the pmu_set_broadcast_awake fun to enable the broadcast wakeup;
	And use pmu_set_broadcast_awake_port set the wakeup port.If you don't call this fun, default port is 9999;