# MLIR
MLIR_ROOT        := /home/jason/.local/lib/python3.10/site-packages/mlir_wheel
MLIR_TOOL_DIR    := $(MLIR_ROOT)/bin
MLIR_INCLUDE_DIR := $(MLIR_ROOT)/include
MLIR_LIB_DIR     := $(MLIR_ROOT)/lib
TBLGEN := $(MLIR_TOOL_DIR)/mlir-tblgen
CXX    := c++
AR     := ar

# Project
PROJECT     := tinyir
OPT         := $(PROJECT)-opt
SRC_DIR 	:= src
DIALECT_DIR := $(SRC_DIR)/dialect
BUILD_DIR   := build
TEST_DIR    := test

# 存放自动生成的.inc文件
GEN_DIR     := $(BUILD_DIR)/gen
OBJ_DIR     := $(BUILD_DIR)/obj
LIB_DIR     := $(BUILD_DIR)/lib
BIN_DIR     := $(BUILD_DIR)/bin

# src
TD_SRCS      := $(wildcard $(DIALECT_DIR)/*.td)
DIALECT_SRCS := $(wildcard $(DIALECT_DIR)/*.cpp)
DIALECT_HDRS := $(wildcard $(DIALECT_DIR)/*.h)
OPT_SRC      := $(SRC_DIR)/$(OPT).cpp
TEST         := $(TEST_DIR)/mytest2.mlir

# outputs
DIALECT_OBJS := $(patsubst $(DIALECT_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(DIALECT_SRCS))
OPT_OBJ      := $(OBJ_DIR)/$(OPT).o
LIB          := $(LIB_DIR)/lib$(PROJECT).a
BIN          := $(BIN_DIR)/$(OPT)
TBLGEN_STAMP := $(GEN_DIR)/.tblgen.stamp

# flags
CPPFLAGS := -I$(MLIR_INCLUDE_DIR) -I$(DIALECT_DIR) -I$(GEN_DIR)
CXXFLAGS := -std=c++17 -fPIC -MMD -MP
TBLGEN_FLAGS := -I$(DIALECT_DIR) -I$(MLIR_INCLUDE_DIR) --write-if-changed
LDFLAGS := -L $(MLIR_LIB_DIR) -Wl,-rpath,$(MLIR_LIB_DIR)
LDLIBS := -lMLIR -lLLVM

# 声明伪目标, 避免被当前目录下的同名文件干扰从而跳过执行make XXX
.PHONY: all build tblgen test clean

all: build

build: $(BIN)

tblgen: $(TBLGEN_STAMP)

### +TBLGEN .td -> .inc
$(TBLGEN_STAMP): $(TD_SRCS)
	@mkdir -p $(GEN_DIR)
	@for td in $(TD_SRCS); do \
		name=$$(basename $$td .td); \
		for spec in \
			"dialect-decls:Dialect.h.inc" \
			"dialect-defs:Dialect.cpp.inc" \
			"typedef-decls:Types.h.inc" \
			"typedef-defs:Types.cpp.inc" \
			"op-decls:Ops.h.inc" \
			"op-defs:Ops.cpp.inc"; do \
			gen=$${spec%%:*}; \
			suffix=$${spec#*:}; \
			out="$(GEN_DIR)/$${name}_$${suffix}"; \
			echo "+TBLGEN  $$td --> $$out"; \
			$(TBLGEN) -gen-$$gen $(TBLGEN_FLAGS) $$td -o $$out || exit 1; \
		done; \
	done
	@touch $@

### +CXX dialect/*.cpp -> .o
$(OBJ_DIR)/%.o: $(DIALECT_DIR)/%.cpp $(TBLGEN_STAMP)
	@mkdir -p $(OBJ_DIR)
	@echo "+CXX     $< --> $@"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-c $< \
		-o $@

### +AR .o -> .a
$(LIB): $(DIALECT_OBJS)
	@mkdir -p $(LIB_DIR)
	@echo "+AR      $(DIALECT_OBJS) --> $(LIB)"
	@$(AR) rcs $@ $^

### +CXX tinyir-opt.cpp -> .o
$(OPT_OBJ): $(OPT_SRC) $(DIALECT_HDRS) $(TBLGEN_STAMP)
	@mkdir -p $(OBJ_DIR)
	@echo "+CXX     $(OPT_SRC) --> $(OPT_OBJ)"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-c $(OPT_SRC) \
		-o $@

### +LD .o, .a -> bin
$(BIN): $(OPT_OBJ) $(LIB)
	@mkdir -p $(BIN_DIR)
	@echo "+LD      $(OPT_OBJ) $(LIB) --> $(BIN)"
	@$(CXX) $^ \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $@

### +TEST .mlir -> stdout
test: $(BIN)
	@echo "+TEST    $(TEST) --> stdout"
	@$(BIN) $(TEST)

-include $(OBJ_DIR)/*.d

clean:
	@echo "+CLEAN   $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)
