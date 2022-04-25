all:	model

CXXFLAGS = -std=c++11

model:	main.cpp DSSimul.h contextes.h
	c++ -o model -std=c++11 main.cpp -lpthread

