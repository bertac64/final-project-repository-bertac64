################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../liblog/log.c 

C_DEPS += \
./liblog/log.d 

OBJS += \
./liblog/log.o 


# Each subdirectory must supply rules for building sources it contributes
liblog/%.o: ../liblog/%.c liblog/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-liblog

clean-liblog:
	-$(RM) ./liblog/log.d ./liblog/log.o

.PHONY: clean-liblog

