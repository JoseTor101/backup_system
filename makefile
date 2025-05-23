# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17
LDFLAGS = -lzip -lssl -lcrypto

# Target executables
TARGETS = main descompresor

# Source files
SRCS_MAIN = main.cpp compress.cpp crypto.h
SRCS_DECOMP = decompress.cpp crypto.h

# Object files
OBJS_MAIN = $(SRCS_MAIN:.cpp=.o)
OBJS_DECOMP = $(SRCS_DECOMP:.cpp=.o)

# Default target
all: $(TARGETS)

# Build the main target
main: $(OBJS_MAIN)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Build the descompresor target
descompresor: $(OBJS_DECOMP)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Rule to build object files
%.o: %.cpp
	$(CXX) -c $< $(CXXFLAGS)

# Clean up build files
clean:
	rm -f $(TARGETS) *.o

# Run main program
run: main
	./main

# Run descompressor
decompress: descompresor
	./descompresor

# Help target
help:
	@echo "Targets disponibles:"
	@echo "  all        : Compila ambos ejecutables (main y descompresor)"
	@echo "  main       : Compila solo el compresor"
	@echo "  descompresor: Compila solo el descompresor"
	@echo "  run        : Ejecuta el compresor"
	@echo "  decompress : Ejecuta el descompresor"
	@echo "  clean      : Elimina archivos compilados"
	@echo "  help       : Muestra esta ayuda"

format:
	@echo "Formateando código fuente..."
	@find . -name "*.cpp" -o -name "*.h" | xargs clang-format -i
	@echo "Formato completado"