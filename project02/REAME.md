1. 자료구조 추가
    ptable 은 그대로 
    proc 에 level, prioirty, passed_time 추가
    
    queue 구현
    prioirty 큐 구현
    mlfq (구현)


2. scheduler 알고리즘 추가 
    mlfq 에서 level 1, level 2, level 3 (priority) 구현
    1초 지날때마다 감소하는 그런 알고리즘
    100초 지날떄마다 boost 하는 그런거 

----------------------------------------

3.  MoQ 구현 



----
프로세스가 바뀔떄?
Timer interrupt, yield, sleep 
이때 스케줄러로 옮겨야 하는데 이때 스케줄러는 
RUNNABLE 한 프로세스가 존재하는 큐중 가장 레벨이 높은 큐 선택 ㅇ