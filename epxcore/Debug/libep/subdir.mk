################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../libep/ep.c \
../libep/util.c 

O_SRCS += \
../libep/ep.o \
../libep/util.o 

C_DEPS += \
./libep/ep.d \
./libep/util.d 

OBJS += \
./libep/ep.o \
./libep/util.o 


# Each subdirectory must supply rules for building sources it contributes
libep/%.o: ../libep/%.c libep/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-libep

clean-libep:
	-$(RM) ./libep/ep.d ./libep/ep.o ./libep/util.d ./libep/util.o

.PHONY: clean-libep

