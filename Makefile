CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -Ipico
PICO_BOARD ?= pico2

build/dashboard: ground/dashboard.cpp
	mkdir -p build
	$(CXX) -std=c++17 -Wall -Wextra -pedantic ground/dashboard.cpp -o $@

host: build/dashboard

build/simulation/simulate: simulation/simulate.cpp pico/flight.hpp pico/cansat.hpp
	mkdir -p build/simulation
	$(CXX) $(CXXFLAGS) simulation/simulate.cpp -o $@

demo: build/simulation/simulate build/dashboard
	mkdir -p simulation/logs
	build/simulation/simulate > simulation/logs/simulated-flight.csv
	build/simulation/simulate --fault bmp > simulation/logs/simulated-bmp.csv
	build/simulation/simulate --fault crc > simulation/logs/simulated-crc.csv
	build/simulation/simulate --fault drop > simulation/logs/simulated-drop.csv
	build/simulation/simulate --fault tx > simulation/logs/simulated-tx.csv
	build/dashboard simulation/logs/simulated-dashboard.csv < simulation/logs/simulated-flight.csv > /dev/null

check: build/simulation/simulate
	$(CXX) $(CXXFLAGS) tests/check.cpp -o /tmp/cansat-check
	/tmp/cansat-check
	sh tests/dashboard_check.sh
	build/simulation/simulate > /dev/null
	build/simulation/simulate --fault bmp > /dev/null
	build/simulation/simulate --fault crc > /dev/null
	build/simulation/simulate --fault drop > /dev/null
	build/simulation/simulate --fault tx > /dev/null
	python3 simulation/circuit_check.py

circuit:
	python3 simulation/circuit_server.py

firmware:
	cmake -S . -B build/$(PICO_BOARD)-arm -DPICO_SDK_PATH=$$PICO_SDK_PATH -DPICO_BOARD=$(PICO_BOARD) -DPICO_TOOLCHAIN_PATH=$$PICO_TOOLCHAIN_PATH
	cmake --build build/$(PICO_BOARD)-arm
