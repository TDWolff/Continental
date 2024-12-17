CXX = g++
CXXFLAGS = -std=c++17

LIBS = -lboost_system -lminiupnpc -lcurl

TARGET = chat

SRCS = chat.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) -o $(TARGET) $(SRCS) $(CXXFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)
