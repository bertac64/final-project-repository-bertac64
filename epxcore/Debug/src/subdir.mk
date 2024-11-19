################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/command.c \
../src/epxcore.c \
../src/main.c \
../src/sharedVar.c \
../src/util.c 

C_DEPS += \
./src/command.d \
./src/epxcore.d \
./src/main.d \
./src/sharedVar.d \
./src/util.d 

OBJS += \
./src/command.o \
./src/epxcore.o \
./src/main.o \
./src/sharedVar.o \
./src/util.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/command.d ./src/command.o ./src/epxcore.d ./src/epxcore.o ./src/main.d ./src/main.o ./src/sharedVar.d ./src/sharedVar.o ./src/util.d ./src/util.o

.PHONY: clean-src

