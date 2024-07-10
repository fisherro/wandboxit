all: wandboxit

wandboxit: wandboxit.cpp
	g++-mp-13 -std=c++17 wandboxit.cpp -o wandboxit -lcurl

