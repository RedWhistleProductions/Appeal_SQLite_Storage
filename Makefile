PLUGIN_NAME := Appeal_SQLite_Storage
SRC_DIR := src
OBJ_ROOT := obj
SQLITE_DIR ?= .deps/sqlite
CXX ?= g++
CC ?= gcc
CPPFLAGS += -I$(SQLITE_DIR)
CXXFLAGS ?= -std=c++17 -Wall
CFLAGS ?= -Wall -DSQLITE_THREADSAFE=1 -DSQLITE_ENABLE_FTS5
LDFLAGS ?= -shared

ifeq ($(OS),Windows_NT)
  PLATFORM := windows
  TARGET := $(PLUGIN_NAME).dll
else
  PLATFORM := linux
  TARGET := $(PLUGIN_NAME).so
  CXXFLAGS += -fPIC
  CFLAGS += -fPIC
endif

OBJ_DIR := $(OBJ_ROOT)/$(PLATFORM)
OBJS := $(OBJ_DIR)/main.o $(OBJ_DIR)/sqlite3.o

all: check-dependencies $(TARGET)

check-dependencies:
	@test -f "$(SQLITE_DIR)/sqlite3.h" || { echo "Missing $(SQLITE_DIR)/sqlite3.h"; exit 1; }
	@test -f "$(SQLITE_DIR)/sqlite3.c" || { echo "Missing $(SQLITE_DIR)/sqlite3.c"; exit 1; }

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/main.o: $(SRC_DIR)/main.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/sqlite3.o: $(SQLITE_DIR)/sqlite3.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_ROOT) $(PLUGIN_NAME).dll $(PLUGIN_NAME).so

.PHONY: all check-dependencies clean