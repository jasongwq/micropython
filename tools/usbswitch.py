#!/usr/bin/env python

# USB interface setting switch utility
# Author: Ibrahim Abd Elqader - 2014/2/7

import sys
import usb.core
import usb.util

# defaults
interface = 2;
altsetting= 0; #MSC

def print_help_exit():
  print "usage: usbswitch.py <msc/hid>"
  sys.exit(1)

if len(sys.argv)!= 2:
    print_help_exit()

if sys.argv[1] == 'msc':
    altsetting = 0
elif sys.argv[1] == 'hid':
    altsetting = 1
else:
    print_help_exit()

# find USB device 
dev = usb.core.find(idVendor=0x0483, idProduct=0x5740)
if dev is None:
    raise ValueError('Device not found')

# detach kernel driver    
if dev.is_kernel_driver_active(interface):
    dev.detach_kernel_driver(interface)

# claim interface
usb.util.claim_interface(dev, interface)

# switch interface setting
dev.set_interface_altsetting(interface, altsetting)

# release interface
#usb.util.release_interface(dev, interface)

#dev.reset()

# reattach kernel driver
#dev.attach_kernel_driver(interface)

