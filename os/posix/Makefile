# Copyright 2012, Qualcomm Innovation Center, Inc.
# 
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
# 
#        http://www.apache.org/licenses/LICENSE-2.0
# 
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
# 


.PHONY: all clean

all: oslib

ifeq "$(OS)" "linux"
  IFCONFIG=IfConfigLinux
else
  IFCONFIG=IfConfigDarwin
endif

oslib:  AdapterUtil.o \
        atomic.o \
        Environ.o \
        Event.o \
        FileStream.o \
        Mutex.o \
        osUtil.o \
        Socket.o \
        Thread.o \
        time.o \
        $(IFCONFIG).o \

%.o:%.cc
	$(CC) $(INCLUDE) $(CXXFLAGS) $(CPPDEFINES)  -c -o $@ $<

clean:
	@rm -f *.o *~

	
