################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../old/epxcore.c 

C_DEPS += \
./old/epxcore.d 

OBJS += \
./old/epxcore.o 


# Each subdirectory must supply rules for building sources it contributes
old/%.o: ../old/%.c old/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-old

clean-old:
	-$(RM) ./old/epxcore.d ./old/epxcore.o

.PHONY: clean-old

