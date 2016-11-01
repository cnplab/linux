obj-y	+= noxs.o
#obj-y	+= xenbus_dev_frontend.o

noxs-objs =
#noxs-objs += noxs_tmp.o
noxs-objs += xenbus_client.o
#noxs-objs += xenbus_comms.o
noxs-objs += noxs_comms.o
#noxs-objs += xenbus_xs.o
noxs-objs += xenbus_probe.o

noxs-be-objs-$(CONFIG_XEN_BACKEND) += xenbus_probe_backend.o
noxs-objs += $(noxs-be-objs-y)

#obj-$(CONFIG_XEN_BACKEND) += xenbus_dev_backend.o
obj-$(CONFIG_XEN_BACKEND) += noxs_dev_backend.o
#obj-$(CONFIG_XEN_XENBUS_FRONTEND) += xenbus_probe_frontend.o

# TODO:
# - decide if module or built-in
# - decide if flag for SYSCTLDEV obj-$(CONFIG_XEN_NETDEV_BACKEND) := xen-netback-noxs.o
#noxs-objs += noxs_sysctlback.o
obj-m += xen-sysctlback-noxs.o
xen-sysctlback-noxs-y := noxs_sysctlback.o
