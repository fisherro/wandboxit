all: wandboxit

wandbox: wandboxit.cpp
	g++-mp-13 -std=c++17 wandboxit.cpp -o wandboxit -lcurl

