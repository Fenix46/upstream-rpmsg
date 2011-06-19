Remote Processor Framework

1. Introduction

Modern SoCs typically have heterogeneous remote processor devices in asymmetric
multiprocessing (AMP) configurations, which may be running different instances
of operating system, whether it's Linux or any other flavor of real-time OS.

OMAP4, for example, has dual Cortex-A9, dual Cortex-M3 and a C64x+ DSP.
In a typical configuration, the dual cortex-A9 is running Linux in a SMP
configuration, and each of the other three cores (two M3 cores and a DSP)
is running its own instance of RTOS in an AMP configuration.

The generic remoteproc driver allows different platforms/architectures to
control (power on, load firmware, power off) those remote processors while
abstracting the hardware differences, so the entire driver doesn't need to be
duplicated.

2. User API

  struct rproc *rproc_get(const char *name);
   - power up the remote processor, identified by the 'name' argument,
     and boot it. If the remote processor is already powered on, the
     function immediately succeeds.
     On success, returns the rproc handle. On failure, NULL is returned.

  void rproc_put(struct rproc *rproc);
   - power off the remote processor, identified by the rproc handle.
     Every call to rproc_get() must be (eventually) accompanied by a call
     to rproc_put(). Calling rproc_put() redundantly is a bug.
     Note: the remote processor will actually be powered off only when the
     last user calls rproc_put().

3. Typical usage

#include <linux/remoteproc.h>

int dummy_rproc_example(void)
{
	struct rproc *my_rproc;

	/* let's power on and boot the image processing unit */
	my_rproc = rproc_get("ipu");
	if (!my_rproc) {
		/*
		 * something went wrong. handle it and leave.
		 */
	}

	/*
	 * the 'ipu' remote processor is now powered on... let it work !
	 */

	/* if we no longer need ipu's services, power it down */
	rproc_put(my_rproc);
}

4. API for implementors

  int rproc_register(struct device *dev, const char *name,
				const struct rproc_ops *ops,
				const char *firmware,
				const struct rproc_mem_entry *memory_maps,
				struct module *owner);
   - should be called from the underlying platform-specific implementation, in
     order to register a new remoteproc device. 'dev' is the underlying
     device, 'name' is the name of the remote processor, which will be
     specified by users calling rproc_get(), 'ops' is the platform-specific
     start/stop handlers, 'firmware' is the name of the firmware file to
     boot the processor with, 'memory_maps' is a table of da<->pa memory
     mappings which should be used to configure the IOMMU (if not relevant,
     just pass NULL here), 'owner' is the underlying module that should
     not be removed while the remote processor is in use.

     Returns 0 on success, or an appropriate error code on failure.

  int rproc_unregister(const char *name);
   - should be called from the underlying platform-specific implementation, in
     order to unregister a remoteproc device that was previously registered
     with rproc_register().

5. Implementation callbacks

Every remoteproc implementation must provide these handlers:

struct rproc_ops {
	int (*start)(struct rproc *rproc, u64 bootaddr);
	int (*stop)(struct rproc *rproc);
};

The ->start() handler takes a rproc handle and an optional bootaddr argument,
and should power on the device and boot it (using the bootaddr argument
if the hardware requires one).
On success, 0 is returned, and on failure, an appropriate error code.

The ->stop() handler takes a rproc handle and powers the device off.
On success, 0 is returned, and on failure, an appropriate error code.

6. Binary Firmware Structure

The following enums and structures define the binary format of the images
remoteproc loads and boot the remote processors with.

The general binary format is as follows:

struct {
      char magic[4] = { 'R', 'P', 'R', 'C' };
      u32 version;
      u32 header_len;
      char header[...] = { header_len bytes of unformatted, textual header };
      struct section {
          u32 type;
          u64 da;
          u32 len;
          u8 content[...] = { len bytes of binary data };
      } [ no limit on number of sections ];
} __packed;

The image begins with a 4-bytes "RPRC" magic, a version number, and a
free-style textual header that users can easily read.

After the header, the firmware contains several sections that should be
loaded to memory so the remote processor can access them.

Every section begins with its type, device address (da) where the remote
processor expects to find this section at (exact meaning depends whether
the device accesses memory through an IOMMU or not. if not, da might just
be physical addresses), the section length and its content.

Most of the sections are either text or data (which currently are treated
exactly the same), but there is one special "resource" section that allows
the remote processor to announce/request certain resources from the host.

A resource section is just a packed array of the following struct:

struct fw_resource {
	u32 type;
	u64 da;
	u64 pa;
	u32 len;
	u32 flags;
	u8 name[48];
} __packed;

The way a resource is really handled strongly depends on its type.
Some resources are just one-way announcements, e.g., a RSC_TRACE type means
that the remote processor will be writing log messages into a trace buffer
which is located at the address specified in 'da'. In that case, 'len' is
the size of that buffer. A RSC_BOOTADDR resource type announces the boot
address (i.e. the first instruction the remote processor should be booted with)
in 'da'.

Other resources entries might be a two-way request/respond negotiation where
a certain resource (memory or any other hardware resource) is requested
by specifying the appropriate type and name. The host should then allocate
such a resource and "reply" by writing the identifier (physical address
or any other device id that will be meaningful to the remote processor)
back into the relevant member of the resource structure. Obviously this
approach can only be used _before_ booting the remote processor. After
the remote processor is powered up, the resource section is expected
to stay static. Runtime resource management (i.e. handling requests after
the remote processor has booted) will be achieved using a dedicated rpmsg
driver.

The latter two-way approach is still preliminary and has not been implemented
yet. It's left to see how this all works out.

Most likely this kind of static allocations of hardware resources for
remote processors can also use DT, so it's interesting to see how
this all work out when DT materializes.
