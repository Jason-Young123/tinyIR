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
SRC_DIR     := src
DIALECT_DIR := $(SRC_DIR)/dialect
PASS_DIR    := $(SRC_DIR)/passes
BUILD_DIR   := build
TEST_DIR    := test

# 存放自动生成/编译文件
GEN_DIR         := $(BUILD_DIR)/gen
OBJ_DIR         := $(BUILD_DIR)/obj
DIALECT_OBJ_DIR := $(OBJ_DIR)/dialect
PASS_OBJ_DIR    := $(OBJ_DIR)/passes
LIB_DIR         := $(BUILD_DIR)/lib
BIN_DIR         := $(BUILD_DIR)/bin

# src
TD_SRCS      := $(wildcard $(DIALECT_DIR)/*.td)
DIALECT_SRCS := $(wildcard $(DIALECT_DIR)/*.cpp)
DIALECT_HDRS := $(wildcard $(DIALECT_DIR)/*.h)
PASS_SRCS    := $(wildcard $(PASS_DIR)/*.cpp)
PASS_HDRS    := $(wildcard $(PASS_DIR)/*.h)
OPT_SRC      := $(SRC_DIR)/$(OPT).cpp

# outputs
DIALECT_OBJS := $(patsubst $(DIALECT_DIR)/%.cpp,$(DIALECT_OBJ_DIR)/%.o,$(DIALECT_SRCS))
PASS_OBJS    := $(patsubst $(PASS_DIR)/%.cpp,$(PASS_OBJ_DIR)/%.o,$(PASS_SRCS))
OPT_OBJ      := $(OBJ_DIR)/$(OPT).o
LIB          := $(LIB_DIR)/lib$(PROJECT).a
BIN          := $(BIN_DIR)/$(OPT)
TBLGEN_STAMP := $(GEN_DIR)/.tblgen.stamp
DEPS         := $(DIALECT_OBJS:.o=.d) $(PASS_OBJS:.o=.d) $(OPT_OBJ:.o=.d)

# flags
CPPFLAGS		:= -I$(MLIR_INCLUDE_DIR) -I$(SRC_DIR) -I$(DIALECT_DIR) -I$(PASS_DIR) -I$(GEN_DIR)
CXXFLAGS 		:= -std=c++17 -fPIC -MMD -MP
TBLGEN_FLAGS 	:= -I$(DIALECT_DIR) -I$(MLIR_INCLUDE_DIR) --write-if-changed
LDFLAGS 		:= -L $(MLIR_LIB_DIR) -Wl,-rpath,$(MLIR_LIB_DIR)
LDLIBS 			:= -lMLIR -lLLVM

# pass
TEST 			?= $(TEST_DIR)/passes/advanced/inspect-ir.mlir 
PASS 			?= 
PASSFLAGS 		:= -mlir-print-ir-before-all -mlir-print-ir-after-all \
					$(if $(strip $(PASS)),--pass-pipeline='builtin.module(func.func($(PASS)))')


# 声明伪目标
.PHONY: all build tblgen pass clean

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
			"attrdef-decls:Attrs.h.inc" \
			"attrdef-defs:Attrs.cpp.inc" \
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
$(DIALECT_OBJ_DIR)/%.o: $(DIALECT_DIR)/%.cpp $(TBLGEN_STAMP)
	@mkdir -p $(DIALECT_OBJ_DIR)
	@echo "+CXX     $< --> $@"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-c $< \
		-o $@

### +CXX passes/*.cpp -> .o
$(PASS_OBJ_DIR)/%.o: $(PASS_DIR)/%.cpp
	@mkdir -p $(PASS_OBJ_DIR)
	@echo "+CXX     $< --> $@"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-c $< \
		-o $@

### +AR .o -> .a
$(LIB): $(DIALECT_OBJS) $(PASS_OBJS)
	@mkdir -p $(LIB_DIR)
	@echo "+AR      $(DIALECT_OBJS) $(PASS_OBJS) --> $(LIB)"
	@$(AR) rcs $@ $^

### +CXX tinyir-opt.cpp -> .o
$(OPT_OBJ): $(OPT_SRC) $(DIALECT_HDRS) $(PASS_HDRS) $(TBLGEN_STAMP)
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


### +PASS .mlir -> stdout
pass: $(BIN)
	@echo "+PASS    $(TEST) --> $(PASS)"
	@$(BIN) $(TEST) \
		$(PASSFLAGS)



-include $(DEPS)

clean:
	@echo "+CLEAN   $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)