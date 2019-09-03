g++ -Iinclude -std=c++17 repro.cpp lib/source.cpp -g -O2 -lstdc++ -lpthread -lm -o repro_with_sem_front
g++ -Iinclude -std=c++17 repro.cpp lib/source.cpp -g -O2 -lstdc++ -lpthread -lm -D__NO_SEM_FRONT -o repro_with_no_sem_front
