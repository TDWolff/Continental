# Set up source and object files
SRC = chat.cpp
OBJ = $(SRC:.cpp=.o)

# Set up the output binary names for both platforms
OUT_MAC = chat_mac
OUT_WIN = chat_win.exe

# Compiler and linker settings
CXX_MAC = clang++
CXX_WIN = g++

# Compiler flags for macOS and Windows
CXXFLAGS_MAC = -std=c++17 -Wall -I/usr/local/include
CXXFLAGS_WIN = -std=c++17 -Wall -I"C:/boost/include"

# Linker flags for macOS and Windows
LDFLAGS_MAC = -L/usr/local/lib
LDFLAGS_WIN = -L"C:/boost/lib"

# Libraries for macOS and Windows
LIBS_MAC = -lboost_system -lminiupnpc -lcurl
LIBS_WIN = -lboost_system -lminiupnpc -lcurl

# Define the platform
ifeq ($(OS),Windows_NT)
    PLATFORM = win
else
    PLATFORM = mac
endif

# Define the final binary name based on the platform
ifeq ($(PLATFORM),win)
    OUTPUT = $(OUT_WIN)
    CXXFLAGS = $(CXXFLAGS_WIN)
    LDFLAGS = $(LDFLAGS_WIN)
    LIBS = $(LIBS_WIN)
else
    OUTPUT = $(OUT_MAC)
    CXXFLAGS = $(CXXFLAGS_MAC)
    LDFLAGS = $(LDFLAGS_MAC)
    LIBS = $(LIBS_MAC)
endif

# Target to compile source files
$(OBJ): $(SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Target to link the object files to create the executable
$(OUTPUT): $(OBJ)
	@echo "Linking $(OUTPUT)..."
	$(CXX) $(OBJ) -o $(OUTPUT) $(LDFLAGS) $(LIBS)

# macOS specific target to create the .app bundle
mac_app: $(OBJ)
	@echo "Creating macOS app bundle..."
	mkdir -p $(OUT_MAC)/Contents/MacOS
	cp $(OBJ) $(OUT_MAC)/Contents/MacOS/
	# Optionally, you can add an Info.plist or other necessary files for the app bundle
	@echo "macOS app bundle created at $(OUT_MAC)"

# Clean up object files and executables
clean:
	rm -f $(OBJ) $(OUT_MAC) $(OUT_WIN)
	rm -rf chat_mac.app

# Rebuild everything
rebuild: clean $(OUTPUT)

# Conditional targets for platform-specific builds
ifeq ($(PLATFORM),win)
  build: $(OUT_WIN)
else
  build: mac_app
endif
