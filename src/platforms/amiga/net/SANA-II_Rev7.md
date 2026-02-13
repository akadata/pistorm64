{{WIP}}
= SANA-II Network Device Driver Specification =

The "SANA-II Network Device Driver Specification" is a standard for an Amiga software interface between networking hardware and network protocol stacks (or for software tools such as network monitors). A network protocol stack is a layer of software that network applications use to address particular processes on remote machines and to send data reliably in spite of hardware errors. There are several common network protocol stacks including ''TCP/IP'', ''OSI'', ''AppleTalk'', ''DECNet'' and ''Novell''.

SANA-II device drivers are intended to allow multiple network protocol stacks running on the same machine to share one network device. For example, the TCP/IP and AppleTalk protocol stacks could both run on the same machine over one ethernet board. The device drivers are also intended to allow network protocol stacks to be written in a hardware-independent fashion so that a different version of each protocol stack doesn't have to be written for each networking hardware device.

The standard does not address the writing of network applications. Application writers must not use SANA-II Device Drivers directly. Network applications must use the API provided by the network protocol software the application supports. The Amiga standard network API for TCP/IP is provided via the bsdsocket.library which is a part of Roadshow.

To write a SANA-II device driver, you will need to be familiar with the specification documents for the hardware you are writing to and with the "SANA-II Network Device Driver Specification".

To write a network protocol stack which will use SANA-II device drivers, you should have general familiarity with common network hardware and must be very familiar with the "SANA-II Network Device Driver Specification" as well as the specification for the protocol you are developing. If you are creating a new protocol, you must obtain a protocol type number for any hardware on which your protocol will be used.

This version of the specification is final. Any new version of the standard (i.e., to add new features) is planned to be backward compatible. No SANA-II device driver or software utilizing those drivers should be written to any earlier version of the specification.

Distribution of this version of the standard is unlimited. Anyone may write Amiga software which implements a SANA-II network device driver or which calls a SANA-II network device driver without restriction and may freely distribute such software that they have written.

It is important to try to test each SANA-II device driver against all software which uses SANA-II devices. Available example programs are valuable in initial testing. The Amiga Networking Group is interested in receiving evaluation and/or beta test copies of all Amiga networking hardware, SANA-II device drivers and software which uses SANA-II devices. However, we make no assurances regarding any testing which we may or may not perform with such evaluation copies.

The SANA-II standard caters to both Motorola 68000 platforms and PowerPC platforms.

Please feel free to comment. You can contact the AmigaOS development team through the [http://www.amigaos.net/contact AmigaOS contact form].

= Driver Form =

SANA-II device drivers are Amiga Exec device drivers. They use an extended IORequest structure and a number of extended commands for tallying network statistics, sending broadcasts and multicasts, network addressing and the handling of unexpected packets. The SDK includes information on how to construct an Exec device.

= Opening a SANA-II Device =

As when opening any other Exec device, on the call to OpenDevice() a SANA-II device receives an IORequest structure which the device initializes for the opener's use. The opener must copy this structure if it desires to use multiple asynchronous requests. The SANA-II IORequest is defined as follows:

<syntaxhighlight lang="C" line>
struct IOSana2Req
{
  struct IORequest ios2_Req;
  ULONG ios2_WireError;
  ULONG ios2_PacketType;
  UBYTE ios2_SrcAddr[SANA2_MAX_ADDR_BYTES];
  UBYTE ios2_DstAddr[SANA2_MAX_ADDR_BYTES];
  ULONG ios2_DataLength;
  APTR ios2_Data;
  APTR ios2_StatData;
  APTR ios2_BufferManagement;
};
</syntaxhighlight>

; ios2_Req
: A standard Exec device IORequest.
; ios2_WireError
: A more specific device code which may be set when there is an io_Error. See &lt;devices/sana2.h&gt; for the defined WireErrors.
; ios2_PacketType
: The type of packet requested. See the section on "Packet Types".
; ios2_SrcAddr
: The device fills in this field with the interface (network hardware) address of the source of the packet that satisfied a read command. The bytes used to hold the address will be left justified but the bit layout is dependent on the particular type of network.
; ios2_DstAddr
: Before the device user sends a packet, it fills this with the interface destination address of the packet. On receives, the device fills this with the interface destination address. Other commands may use this field differently (see the "SANA-II network device driver Autodocs" in the SDK). The bytes used to hold the address will be left justified but the bit layout is dependent on the particular type of network.
; ios2_DataLength
: The device user initializes this field with the amount of data available in the Data buffer before passing the IOSana2Req to the device. The device fills in this field with the size of the packet data as it was sent on the wire. This does not include the header and trailer information. Depending on the network type and protocol type, the driver may have to calculate this value. This is generally used only for reads and writes (including broadcast and multicast).
; ios2_Data
: A pointer to some abstract data structure containing packet data. ''Drivers may not directly manipulate or examine anything pointed to by Data!'' This is generally used only for reads and writes (including broadcast and multicast).

; ios2_StatData
: Pointer to a structure in which to place a snapshot of device statistics. The data area must be long word aligned. This is only used on calls to the statistics commands.

; ios2_BufferManagement
: The opener places a pointer to a tag list in this field before calling OpenDevice(). Functions pointed to in the tag list are called by the device when processing IORequests from the opener. When returned from OpenDevice(), this field contains a pointer to driver-private information used to access these functions. See "Buffer Management" below for more details.

: Note that the <tt>ios2_BufferManagement</tt> field provided by the driver on <tt>OpenDevice()</tt> time in conjunction with <tt>io_Device</tt> and <tt>io_Unit</tt> is the unique identifier for all requests coming from this protocol stack until <tt>CloseDevice()</tt>. The driver must not ever change the <tt>ios2_BufferManagement</tt> field for a protocol stack at run time, even if <tt>S2_SANAHOOK</tt> is called to request extended features.

The flags used with the device on OpenDevice() are (SANA2OPB_xxx):

{| class="wikitable"
! Name
! Description
|-
| SANA2OPB_MINE
| Exclusive access to the unit requested.
|-
| SANA2OPB_PROM
| Promiscuous mode requested. Hardware which supports promiscuous mode allows all packets sent over the wire to be captured whether or not they are addressed to this node.</br>
'''''Note:''' Promiscuous mode requires exclusive opening of the device.''
|}

The flags used during I/O requests are (SANA2IOB_xxx):

{| class="wikitable"
! Name
! Description
|-
| SANA2IOB_RAW
| Raw packet read/write requested. Raw packets should include the entire data-link layer packet. Devices with the same hardware device number should have the same raw packet format.
|-
| SANA2IOB_BCAST
| Broadcast packet (received).
|-
| SANA2IOB_MCAST
| Multicast packet (received).
|-
| SANA2IOB_QUICK
| Quick IO requested.
|}

= Buffer Management =

Unlike most other Exec Device drivers, SANA-II drivers have no internal buffers. Instead, they read/write to/from an abstract data structure allocated by the driver user. The driver accesses these buffers only via functions that the driver user provides to the driver. The driver user must provide two functions: one copies data to the abstract data structure and one copies data from the abstract data structure. The driver user can therefore choose the data structure used for buffer management by both the driver and driver user in order to have efficient memory and CPU usage overall.

The IOSana2Req contains a pointer to data and the length of said data. A driver is not allowed to make assumptions about how the data is stored. The driver cannot directly manipulate or examine the buffer in any manner. The driver can only access the buffer by calling the functions provided by the driver user.

Before calling OpenDevice(), the driver user points ios2_BufferManagement to a list of tags (defined in &lt;devices/sana2.h&gt;) which include pointers to the buffer management functions required by the driver (defined below). The driver will fail to open if the driver user does not supply all of the required functions. If the device opens successfully, the driver sets ios2_BufferManagement to a value which this opener must use in all future calls to the driver. This "magic cookie" is used from then on to access these functions (a "magic cookie" is a value which one software entity passes to another but which is only meaningful to one of the software entities). The driver user may not use the "magic cookie" in any way--it is for the driver to do with as it wishes. The driver could in theory choose to just copy the tag list to driver-owned memory and then parse the list for every IORequest, but it is much more efficient for the driver to create some sort of table of functions and to point ios2_BufferManagement to that table.

Another recommendation for the ``magic cookie`` is to use it to maintain a separate packet read queue for each device opener. This would allow multiple protocol stacks that all wish to receive the same packet type to work together without having to "know" about each other as ''Envoy'' and ''AS225'' do right now. What does multiple protocol stack support mean? Basically this means that each opener gets all the packets necessary. If a packet comes in that fills a request for more than one opener of the device, all of them will get a copy of the packet. This feature should never be left out of a device design. If it is missing, the usefulness of the device is severely limited.

In order to help system load, a packet filter callback allows protocol stacks to reject packets that are known to not be useful. ''Envoy'''s nipc.library (for example) could be modified to reject TCP packets (as it never uses them).

The specification currently defines the following tags for the OpenDevice() ios2_BufferManagement tag list:

{| class="wikitable"
|+ Copy data from network interface to protocol stack buffer
! Tag
! Description
|-
| S2_CopyToBuff (mandatory)
| This is a pointer to a function which conforms to the CopyToBuff Autodoc.
|-
| S2_CopyToBuff16 (optional)
| Copy to a 16 bit aligned buffer using 16 bit data words.
|-
| S2_CopyToBuff32 (optional)
| Copy to a 32 bit aligned buffer using 32 bit data words.
|-
| S2_DMACopyToBuff32 (optional)
| Perform a DMA copy to a 32 bit aligned contiguous buffer using 32 bit data words.
|-
| S2_DMACopyToBuff64 (optional)
| Perform a DMA copy to a 64 bit aligned contiguous buffer using 64 bit data words.
|}

{| class="wikitable"
|+ Copy data from protocol stack buffer to network interface
! Tag
! Description
|-
| S2_CopyFromBuff (mandatory)
| This is a pointer to a function which conforms to the CopyFromBuff Autodoc.
|-
| S2_CopyFromBuff16 (optional)
| Copy from a 16 bit aligned buffer using 16 bit data words.
|-
| S2_CopyFromBuff32 (optional)
| Copy from a 32 bit aligned buffer using 32 bit data words.
|-
| S2_DMACopyFromBuff32 (optional)
| Perform a DMA copy from a 32 bit aligned contiguous buffer using 32 bit data words.
|-
| S2_DMACopyFromBuff64 (optional)
| Perform a DMA copy from a 64 bit aligned contiguous buffer using 64 bit data words.
|}

{| class="wikitable"
|+ Filter data before protocol stack buffer
! Tag
! Description
|-
| S2_PacketFilter (optional)
| This is a pointer to a standard Hook to be called before S2_CopyToBuff is done. See the PacketFilter Autodoc for more information.
|}

{| class="wikitable"
|+ Logging messages from the driver
! Tag
! Description
|-
| S2_Log (optional)
| This is a pointer to a standard Hook to be called when the driver logs events.
|}

== Better buffer management ==

The mandatory buffer management callbacks may not be very efficient for certain types of hardware. They also do not allow driver DMA access.

All the new features are completely optional and do not collide with existing features. They may be used only when the protocol stack asks for them on opening a driver.

The enhancements consist of several new tags that may be specified by a protocol stack on OpenDevice() to offer certain data transfer options. It is up to the device driver to chose which callbacks to use at what time. These tags are advisory only and may be ignored by the driver for any data buffer at any time:

{| class="wikitable"
| S2_CopyToBuff16
|-
| S2_CopyFromBuff16
|-
| S2_CopyToBuff32
|-
| S2_CopyFromBuff32
|}

These are optional callbacks presented to the device with the same calling interface as for S2_CopyToBuff or S2_CopyFromBuff, respectively. The difference to the original callbacks is the required and guaranteed transfer size and alignment for accessing the device's buffer for a single piece of a data of either 16 or 32 bits, a data word. The copy function called may only use 16/32 bit aligned read/write commands of 16/32 bits at once to transfer the data words, respectively. If the buffer data length is not a multiple of the required data word transfer size, the last data word transfer may contain garbage padding in either transfer direction.

The following tags have been added to support direct writes into hardware buffers that do not allow arbitrarily sized or aligned accesses:

{| class="wikitable"
| S2_DMACopyToBuff32
|-
| S2_DMACopyFromBuff32
|}

If the protocol stack wants to optionally enhance data transfer efficiency with DMA supporting devices, it may pass any of these optional tags to the device on OpenDevice().

If the device driver supports DMA, it may call the respective callback with the abstract magic cookie ios2_Data in register A0. The callback may return NULL in D0. In this case, the driver may not use DMA for this buffer. Alternatively, the callback may return the address of the actual data buffer in D0, if it has these characteristics:

* The buffer is in contiguous memory. Depending on the intended data direction, it shall be readable or writable.
* The buffer is aligned on a 32 bit boundary.
* The buffer size shall be a multiple of 32 bit and it is at least = ios2_DataLength.
* It is up to the driver to decide if it can use DMA for this buffer and it shall fall back to the standard CPU callbacks if necessary. The data transfer method actually used by the driver will not be known in advance by the protocol stack.

The following tags have been added to allow for 64 bit aligned PCI DMA accesses to take place:

{| class="wikitable"
| S2_DMACopyToBuff64
|-
| S2_DMACopyFromBuff64
|}

These two callbacks are identical in operation to the <tt>S2_DMACopyToBuff32</tt> and <tt>S2_DMACopyFromBuff32</tt> callbacks. The difference is in that the memory region DMA is to take place in must be aligned to a 64 bit boundary and must be large enough to hold data that is a multiple of 64 bits in size.

== Best buffer management ==

The SANA-II driver interface is intended to transform data between the hardware layer and the link layer, to be used by networking software such as TCP/IP stacks. This transformation is performed by callback functions which are supplied by the networking software at the time the device driver is opened. The device driver then invokes these functions later in order to transfer data received and data to be sent.

The function parameters used by these callbacks are passed in 68000 registers for the lowest overhead. The problem with 68000 register parameters is that on the PowerPC platform, this form of parameter passing may require the use of emulation code. This is costly and may incur a severe performance penalty. It is an even greater problem if PowerPC native networking software is calling PowerPC native networking driver software and the other way round. In both cases the runtime environment will have to enter emulation mode, return to to PowerPC native execution, dip into emulation mode and return to native PowerPC execution. It would be much better if the chain of execution would stay in PowerPC mode all the time.

Alternatively, if a hook function is used, the operating system may be able to decide whether the function to be invoked needs emulating or called directly. The <tt>Hook</tt> effectively works as an abstraction which makes the function invocation platform independent.

The following standard wraps the copying and logging functions into the standardized <tt>Hook</tt> interface.

==== The hook function ====

The hook function itself is invoked with the following parameters:

<syntaxhighlight lang="C" line>
   result = hook_function(hook, sana2req, sana2hookmsg)

   ULONG hook_function(struct Hook *hook,
                       struct IOSana2Req *sana2req,
                       struct SANA2HookMsg *sana2hookmsg);
</syntaxhighlight>

Note that the result is not necessarily of type <tt>ULONG</tt>. It is a 32 bit value, which can be a boolean result code (for <tt>CopyFromBuff</tt>, <tt>CopyToBuff</tt> and their like) or a pointer to a memory address (for <tt>DMACopyToBuff</tt>, <tt>DMACopyFromBuff</tt> and their like).

=== Data structures ===

The following hook messages are to be used:

<syntaxhighlight lang="C" line>
struct SANA2HookMsg
{
  ULONG shm_Method;
  ULONG shm_MsgSize;
};
</syntaxhighlight>

In this data structure the <tt>shm_Method</tt> field would indicate the task to be performed. This can be a request to copy data or to store a log message. The <tt>shm_MsgSize</tt> field tells you how large the data structure is for future enhancements which may cause the data structure to grow.

==== Copying operations ====

For copying operations the message is:

<syntaxhighlight lang="C" line>
struct SANA2CopyHookMsg
{
  ULONG schm_Method;
  ULONG schm_MsgSize;

  APTR  schm_To;
  APTR  schm_From;
  ULONG schm_Size;
};
</syntaxhighlight>

The structure members are as follows:

; schm_Method
: This must be one <tt>S2_CopyToBuff</tt>, <tt>S2_CopyFromBuff</tt>, <tt>S2_CopyToBuff16</tt>, <tt>S2_CopyFromBuff16</tt>, <tt>S2_CopyToBuff32</tt>, <tt>S2_CopyFromBuff32</tt>, <tt>S2_DMACopyToBuff32</tt>, <tt>S2_DMACopyFromBuff32</tt>, <tt>S2_DMACopyToBuff64</tt> or <tt>S2_DMACopyFromBuff64</tt> to identify the function to be performed.

; schm_MsgSize
: Size of this message data structure in bytes. This must be &gt;= 20 for this message type.
: The driver shall set <tt>schm_MsgSize</tt> always correctly to be compliant. The protocol stack shall use this field to validate the message and to reject/ignore bad messages via a <tt>FALSE</tt> hook function return value. For DMA related hooks, a <tt>FALSE</tt> return value is equivalent to a <tt>NULL</tt> pointer.

; schm_To
: Equivalent to the <tt>to</tt> parameter of the <tt>CopyFromBuff</tt>, <tt>CopyToBuff</tt> and <tt>DMACopyToBuff</tt> functions.

; schm_From
: Equivalent to the <tt>from</tt> parameter of the <tt>CopyFromBuff</tt> and <tt>CopyToBuff</tt> functions.

; schm_Size
: Equivalent to the <tt>n</tt> parameter of the <tt>CopyFromBuff</tt>, <tt>CopyToBuff</tt>, functions.

==== Logging operations ====

For logging operations the message is:

<syntaxhighlight lang="C" line>
struct SANA2LogHookMsg
{
  ULONG  slhm_Method;
  ULONG  slhm_MsgSize;

  ULONG  slhm_Priority;
  STRPTR slhm_Name;
  STRPTR slhm_Message;
};
</syntaxhighlight>

The structure members would be used as follows:

; slhm_Method
: This must be <tt>S2_Log</tt>, as defined in the SANA-IIR4 specification.

; slhm_MsgSize
: Size of this message data structure in bytes. This must be &gt;= 20 for this message type.
: The driver shall set <tt>slhm_MsgSize</tt> always correctly to be compliant. The protocol stack shall use this field to validate the message and to reject/ignore bad messages via a <tt>FALSE</tt> hook function return value. For DMA related hooks, a <tt>FALSE</tt> return value is equivalent to a <tt>NULL</tt> pointer.

; slhm_Priority
: The smaller this value, the more important the message to be logged or displayed. The following priority levels are defined (similar to the Unix syslog() mechanism):
:{| class="wikitable"
| S2LOG_Emergency || A panic condition.
|-
| S2LOG_Alert || A condition that should be corrected immediately.
|-
| S2LOG_Critical || Critical conditions.
|-
| S2LOG_Error || A plain error.
|-
| S2LOG_Warning || A warning message.
|-
| S2LOG_Notice || Conditions that are not error conditions, but should possibly be handled specially.
|-
| S2LOG_Information || An informational message.
|-
| S2LOG_Debug || Messages that contain information normally of use only when debugging.
|}
: Only these priority values may be used by a driver. It is suggested that a driver is configurable to generate different types of messages or not, e.g., a driver may be configured to only emit <tt>S2LOG_Emergency</tt> and <tt>S2LOG_Debug</tt> messages

; slhm_Name
: Pointer to a <tt>NUL</tt>-terminated string which identifies the source of this message. This can be <tt>NULL</tt> in which case the OS device name of the driver shall be used by the protocol stack.

; slhm_Message
: Pointer to a <tt>NUL</tt>-terminated string which contains the log message. The text should not contain any formatting characters such as line feeds or carriage returns. The <tt>slhm_Message</tt> member must never be <tt>NULL</tt>.
: All message texts shall preferably be formatted in the current user's locale. If that is not possible, the english language shall be used. <tt>slhm_Name</tt> and <tt>slhm_Message</tt> shall only contain printable characters.

=== Application and driver software use of the new functions ===

Since plenty of software exists which uses the 'traditional' <tt>TagItem</tt> list provided at <tt>OpenDevice()</tt> time, drivers must always examine these parameters and not expect a <tt>S2_SANA2HOOK</tt> command to be sent later.

If the new <tt>Hook</tt>-based callback functions are used then the driver must invoke the Hooks via <tt>utility.library/CallHookPkt</tt>. It must never invoke the hook functions through local assembly language stubs or the <tt>amiga.lib/CallHook</tt> and <tt>amiga.lib/CallHookA</tt> functions.

=== Caveats ===

The functionality above suggests that one could do entirely without the <tt>TagItem</tt> list passed in at <tt>OpenDevice()</tt> time. However, at this time it is hard to tell how existing driver software will react to empty <tt>TagItem</tt> lists or even a NULL pointer in the <tt>IOSana2Req-&gt;ios2_BufferManagement</tt> field. It is therefore important to always provide for a <tt>TagItem</tt> list which includes proper (i.e. they must point to working functions and may not be <tt>NULL</tt>) function pointers for the <tt>S2_CopyToBuff</tt> and <tt>S2_CopyFromBuff</tt> tags. Once the device has been opened successfully, the next step is to try and install the copy <tt>Hook</tt> through the proposed <tt>S2_SANA2HOOK</tt> command. If the command fails, the application can still expect that the <tt>S2_CopyToBuff</tt> and <tt>S2_CopyFromBuff</tt> tags supplied at <tt>OpenDevice()</tt> time will work.

== Packet filtering ==

With the original "SANA-II Network Device Driver Specification", a protocol stack could open a device and ask for certain packet types. It got all the packets that matched this type. As it turned out, this could be mighty inefficient if there were packets that the protocol stack did not use at all. These would go into read processing of the protocol stack and waste CPU time even though they could have been easily identified on arrival.

== Driver event logging ==

A driver may want to report an important event for the user to see. Adding a log message to a file or opening a window to display a message in may not be the optimum approach as the user may be unaware of the context into which the message belongs. It may be advisable for the driver to use the message reporting and logging facilities used by the client software that uses its services, such as a TCP/IP stack. The <tt>S2_Log</tt> callback hook is intended to provide for such a link.

If present, the driver must use this callback hook rather than whatever logging methods it implements itself. Note that unlike the other SANA-II callbacks, this is a regular hook, as to be invoked using <tt>utility.library</tt>/<tt>CallHookPkt()</tt>. The hook function is invoked using the following parameters:

<syntaxhighlight lang="C" line>
log_hook_function(hook, reserved, message)

void log_hook_function(struct hook * hook,APTR reserved,
                       struct S2LogMessage * message);
</syntaxhighlight>

The <tt>reserved</tt> parameter '''must''' be set to <tt>NULL</tt>. The <tt>S2LogMessage</tt> structure passed as the third parameter looks like this:

<syntaxhighlight lang="C" line>
struct S2LogMessage
{
   LONG   s2lm_Size;
   ULONG  s2lm_Priority;
   STRPTR s2lm_Name;
   STRPTR s2lm_Message;
};
</syntaxhighlight>

The individual structure members serve the following functions:

; s2lm_Size
: Size of the <tt>S2LogMessage</tt> structure, in bytes. The idea is to extend this data structure in the future, and the size stored in here tells you how long the structure is. The size '''must''' always be &gt;= 16.

;s2lm_Priority
: The smaller this value, the more important the message to be logged or displayed. The following priority levels are defined (similar to the Unix <tt>syslog()</tt> mechanism):
:{| class="wikitable"
| S2LOG_Emergency || A panic condition.
|-
| S2LOG_Alert || A condition that should be corrected immediately.
|-
| S2LOG_Critical || Critical conditions.
|-
| S2LOG_Error || A plain error.
|-
| S2LOG_Warning || A warning message.
|-
| S2LOG_Notice || Conditions that are not error conditions, but should possibly be handled specially.
|-
| S2LOG_Information || An informational message.
|-
| S2LOG_Debug || Messages that contain information normally of use only when debugging.
|}

; s2lm_Name</tt>
: Pointer to a <tt>NUL</tt>-terminated string which identifies the source of this message. This can be <tt>NULL</tt> in which case the name is treated as being unknown.

; s2lm_Message
: Pointer to a <tt>NUL</tt>-terminated string which contains the log message. The text should not contain any formatting characters such as line feeds or carriage returns. The <tt>s2lm_Message</tt> member '''must never''' be <tt>NULL</tt>.

All error messages issued by the device driver should use the current system locale wherever this is possible. The purpose of an error message is, after all, to assist the user in recovering from the error. Which may be difficult if the user does not even know the language in which the message is written.

The log message string is valid until the log hook function returns. If the driver needs to retain the message any longer, it must make a copy of it.

Since the client software into which the log hook calls may have to allocate memory to hold and display the log message, the log hook '''must not''' be called from interrupt code. The log hook shall not <tt>Wait()</tt> and it shall assume only a <tt>Task</tt> calling context of unknown priority. <tt>dos.library</tt> functions may not be called. Also, stack space is provided only to call <tt>exec.library</tt> and <tt>utility.library</tt> functions. The callback shall not place excessive data on the stack. Stack space should be considered limited and the callback should be designed to be fast and short.

This hook is installed at <tt>OpenDevice()</tt> time, which means that the hook is used for the unit that was opened, and not just for the I/O request it was opened with. The hook must remain installed until the I/O request that installed it is eventually used to close the device. When this happens, the device should fall back to use no log hook at all. No nesting is permitted or required.

= Packet Type =

Network frames always have a type field associated with them. These type fields vary in length, position and meaning by frame type (frame types generally correspond one-to-one with hardware types, but see "Ethernet Packet Types" below). The meanings of the type numbers are always carefully defined and every type number is registered with some official body. Do not use a type number which is not registered for any standard hardware you use or in a manner inconsistent with that registration.

The type field allows the SANA-II device driver to fulfill CMD_READs based on the type of packet the driver user wants. Multiple protocols can therefore run over the same wire using the same driver without stepping on each other's toes.

Packet types are specified as a long word. Unfortunately, the type field means different things on different wires. Driver users must allow their software to be configured with a SANA-II device name, unit number and the type number(s) used by the protocol stack with each device. This way, if new hardware becomes available, a hardware manufacturer can supply a listing of type assignments to configure pre-existing software.

== Ethernet Packet Types ==

Ethernet has a special problem with packet types. Two types of ethernet frames can be sent over the same wire: ethernet and 802.3. These frames differ in that the Type field of an ethernet frame is the Length field of an 802.3 frame. This creates a problem in that demultiplexing incoming packets can be cumbersome and inefficient, as well as requiring driver users to be aware of the frame type used.

All 802.3 frames have numbers less than 1500 in the Type field. The only frames with numbers less than 1500 in the type field are 802.3 frames. SANA-II ethernet drivers abnormally return packets contained in ethernet frames when the requested Type falls within the 802.3 range-if the Type requested is within the 802.3 range, the driver returns the next packet contained within an 802.3 frame, regardless of the type specified for the packet within the 802.3 frame. This requires that there be no more than one driver user requesting 802.3 packets and that it do its own interpretation of the frames.

== ARCNET Frames ==

ARCNET also has a special problem with framing. ARCNET frames consist of a hardware header and a software header. The software header is in the data area of the hardware packet, and includes at least the protocol ID.

There are two types of software header. Old-style ARCNET software headers consist entirely of a one or two byte protocol ID. New ARCNET software headers (defined in RFC 1201 and in the paper "ARCNET Packet Header Definition Standard", Novell, Inc., 1989) include more information. They allow more efficient use of ARCNET through data link layer fragmentation and reassembly (ARCNET has a small Maximum Transmission Unit) and allow sending any size packet up to the MTU (rather than requiring that packets of size 253, 254 and 255 be padded to at least 256 bytes).

SANA-II device drivers for ARCNET should implement the old ARCNET packet headers. Driver users which wish to interoperate with platforms using the new software headers must add the new fields to the data to be sent and must process it for incoming data. A SANA-II driver which implemented the data link layer fragmentation internally (and advertised a large MTU) could be more efficient than requiring the driver user to do it. This would make driver writing more difficult and reduce interoperability, but if there is ever a demand for that extra performance, a new hardware type may be assigned by Amiga for SANA-II ARCNET device drivers which implement the new framing.

= Addressing =

In the SANA-II standard, network hardware addresses are stored in an array of ''n'' bytes. No meaning is ascribed by the standard to the contents of the array.

In case there exists a network which does not have an address field consisting of a number of bits not divisible by eight, add pad bits at the end of the bit stream. For example, if an address is ten bits long it will be stored like this:

<pre>
98765432 10PPPPPP
BYTE 0   BYTE 1
</pre>

Where the numerals are bit numbers and "P" is a pad (ignored) bit.

Driver users which do not implement the bit shifting necessary to use a network with such addressing (if one exists) should at least check the number of significant bits in the address field (returned from the device's S2_DEVICEQUERY function) to make sure that it is evenly divisible by eight.

Driver users will map hardware addresses to protocol addresses in a protocol and hardware dependent manner, as described by the relevant standards (i.e., RFC 826 for TCP/IP over Ethernet, RFC 1201 or RFC 1051 for TCP/IP over ARCNET). Some protocols will always use the same mapping on all hardware, but other protocols will have particular address mapping schemes for some particular hardware and a reasonable default for other (unknown) hardware.

Some SANA-II devices will have "hardware addresses" which aren't really hardware addresses. As an example, consider ''PPP'' (Point-to-Point Protocol). PPP is a standard for transmitting IP packets over a serial line. It uses IP addresses negotiated during the establishment of a connection. In a SANA-II driver implementation of PPP, the driver would negotiate the address at S2_CONFIGINTERFACE. Thus, the address in SrcAddr returned by the device on an S2_CONFIGINTERFACE (or in a subsequent S2_GETSTATIONADDRESS) will be a protocol address, not a true hardware address.

'''Note:''' Some hardware always uses a ROM hardware address. Other hardware which has a ROM address or is configurable with DIP switches may be overridden by software. Some hardware always dynamically allocates a new hardware address at initialization. See "Configuration" for details on how this is handled by driver writers and by driver users.

= Hardware Type =

The HardwareType returned by the device's S2_DEVICEQUERY function is necessary for those protocols whose standards require different behavior on different hardware. It is also useful for determining appropriate packet type numbers to use with the device. The HardwareType values already issued for standard network hardware are the same as those in RFC 1060 (assigned numbers). Hardware developers implementing networks without a SANA-II hardware number must contact the AmigaOS development team to have a new hardware type number assigned. Driver users should all have reasonable defaults which can be used for hardware with which they are not familiar.

= Errors =

The SANA-II extended IORequest structure (struct IOSana2Req) includes both the ios2_Error and ios2_WireError fields. Driver users must always check IOSana2Reqs on return for an error in ios2_Error. ios2_Error will be zero if no error occurred, otherwise it will contain a value from &lt;exec/errors.h&gt; or &lt;devices/sana2.h&gt;. If there was an error, there may be more specific information in ios2_WireError. Drivers are required to fill in the WireError if there is an applicable error code.

Error codes are #define'd in the "defined errors" sections of the file &lt;devices/sana2.h&gt;:<br />

{| class="wikitable"
! IOSana2Req S2io_Error field
! Description
|-
| S2ERR_NO_RESOURCES
| Insufficient resources available.
|-
| S2ERR_BAD_ARGUMENT
| Noticeably bad argument.
|-
| S2ERR_BAD_STATE
| Command inappropriate for current state.
|-
| S2ERR_BAD_ADDRESS
| Noticeably bad address.
|-
| S2ERR_MTU_EXCEEDED
| Write data too large.
|-
| S2ERR_NOT_SUPPORTED
| Command is not supported by this driver. This is similar to IOERR_NOCMD as defined in &lt;exec/errors.h&gt; but S2ERR_NOT_SUPPORTED indicates that the requested command is a valid SANA-II command and that the driver does not support it because the hardware is incapable of supporting it (e.g., S2_MULTICAST). Note that IOERR_NOCMD is still valid for reasons other than a lack of hardware support (i.e., commands which are no-ops in a SANA-II driver).
|-
| S2ERR_SOFTWARE
| Software error of some kind.
|-
| S2ERR_OUTOFSERVICE
| When a hardware device is taken off-line, any pending requests are returned with this error.
|}

See also the standard errors in &lt;exec/errors.h&gt;.

{| class="wikitable"
! IOSana2Req S2io_WireError field
! Description
! Value
|-
| S2WERR_NOT_CONFIGURED
| Command requires unit to be configured.
|-
| S2WERR_UNIT_ONLINE
| Command requires that the unit be off-line.
|-
| S2WERR_UNIT_OFFLINE
| Command requires that the unit be on-line.
|-
| S2WERR_ALREADY_TRACKED
| Protocol is already being tracked.
|-
| S2WERR_NOT_TRACKED
| Protocol is not being tracked.
|-
| S2WERR_BUFF_ERROR
| Buffer management function returned an error.
|-
| S2WERR_SRC_ADDRESS
| Problem with the source address field.
|-
| S2WERR_DST_ADDRESS
| Problem with destination address field.
|-
| S2WERR_BAD_BROADCAST
| Problem with an attempt to broadcast.
|-
| S2WERR_BAD_MULTICAST
| Problem with an attempt to multicast.
|-
| S2WERR_MULTICAST_FULL
| Multicast address list full.
|-
| S2WERR_BAD_EVENT
| Event specified is unknown.
|-
| S2WERR_BAD_STATDATA
| The ios2_StatData pointer or the data it points to failed a sanity check.
|-
| S2WERR_IS_CONFIGURED
| Attempt to reconfigure the unit.
|-
| S2WERR_NULL_POINTER
| A NULL pointer was detected in one of the arguments. S2ERR_BAD_ARGUMENT should always be the S2ERR.
|-
| S2WERR_UNIT_DISCONNECTED
| This error code is a counterpart to <tt>S2WERR_UNIT_OFFLINE</tt>. It indicates that the associated command could not be executed because the link layer is not connected.
| 19
|-
| S2WERR_UNIT_CONNECTED
| This error code is a counterpart to <tt>S2WERR_UNIT_ONLINE</tt>. It indicates that the associated command could not be executed because the link layer is already connected.
| 20
|-
| S2WERR_INVALID_OPTION
| This error code indicates that an option, such as passed by the <tt>S2_CONNECT</tt> command, is not acceptable. The option's value may be out of range or may not match the syntax specifications. To indicate which option that may be, a different mechanism '''must''' be used; a simple indication that something was wrong is '''not sufficient'''.
| 21
|-
| S2WERR_MISSING_OPTION
| This error code indicates that a mandatory option, such as passed by the <tt>S2_CONNECT</tt> command, is not present. To indicate which option that may be, a different mechanism '''must''' be used; a simple indication that something was wrong is '''not sufficient'''.
| 22
|-
| S2WERR_AUTHENTICATION_FAILED
| Some drivers run protocols that require them to authenticate to a server. That process may fail. This wire error code is to indicate this fact.
| 23
|}

= Standard Commands =

See the "SANA-II network device driver Autodocs" for full details on each of the SANA-II device commands. Extended commands are explained in the sections below.

Many of the Exec device standard commands are no-ops in SANA-II devices, but this may not always be the case. For example, CMD_RESET might someday be used for dynamically reconfiguring hardware. This should present no compatibility problems for properly written drivers.

== Broadcast and Multicast ==

Some hardware supports broadcast and/or multicast. A broadcast is a packet sent to all other machines. A multicast is a packet sent to a set of machines. Drivers for hardware which does not allow broadcast or multicast will return ios2_Error S2ERR_NOT_SUPPORTED as appropriate.

To send a broadcast, use S2_BROADCAST instead of CMD_WRITE. Broadcasts are received just like any other packets (using a CMD_READ for the appropriate packet type).

To send a multicast, use S2_MULTICAST instead of CMD_WRITE. The device keeps a list of addresses that want to receive multicasts. You add a receiver's address to this list by using S2_ADDMULTICASTADDRESS. The receiver then posts a CMD_READ for the type of packet to be received. Some SANA-II devices which support multicast may have a limit on the number of addresses that can simultaneously wait for packets. Always check for an S2WERR_MULTICAST_FULL error return when adding a multicast address.

Note that when the device adds a multicast address, it is usually added for all users of the device, not just the driver user which called S2_ADDMULTICASTADDRESS. In other words, received multicast packets will fill a read request of the appropriate type regardless of whether the requesting driver user is the same one which added the multicast address.

In general, driver users should not care how received packets were sent (normally or broadcast/multicast), only that it was received. If a driver user really must know, however, it can check for SANA2IOB_BCAST and/or SANA2IOB_MCAST in the ios2_Flags field.

Drivers should keep a count for the number of opens on a multicast address so that they don't actually remove it until it has been S2_DELMULTICASTADDRESS'd as many times as it has been S2_ADDMULTICASTADDRESS'd.

== Stats ==

There are many statistics which may be very important to someone trying to debug, tune or optimize a protocol stack, as well as to the end user who may need to tune parameters or investigate a problem. Some of these statistics can only be kept by the SANA-II driver, thus there are several required and optional statistics and commands for this purpose.

S2_TRACKTYPE tells the device driver to gather statistics for a particular packet type. S2_UNTRACKTYPE tells it to stop (keeping statistics by type causes the driver to use additional resources). S2_GETTYPESTATS returns any statistics accumulated by the driver for a type being tracked (stats are lost when a type is S2_UNTRACKTYPE'd). Drivers are required to implement the functionality of type tracking. The stats are returned in a struct Sana2PacketTypeStats:

<syntaxhighlight lang="C" line>
struct Sana2PacketTypeStats
{
  ULONG PacketsSent;
  ULONG PacketsReceived;
  ULONG BytesSent;
  ULONG BytesReceived;
  ULONG PacketsDropped;
};
</syntaxhighlight>

; PacketsSent
: Number of packets of a particular type sent.
; PacketsReceived
: Number of packets of a particular type that satisfied a read command.
; BytesSent
: Number of bytes of data sent in packets of a particular type.
; BytesReceived
: Number of bytes of data of a particular packet type that satisfied a read command.
; PacketsDropped
: Number of packets of a particular type that were received while there were no pending reads of that packet type.

returns global statistics kept by the driver. Drivers are required to keep all applicable statistics. Since all are applicable to most hardware, most drivers will maintain all statistics. The stats are returned in a struct Sana2DeviceStats:

<syntaxhighlight lang="C" line>
struct Sana2DeviceStats
{
  ULONG PacketsReceived;
  ULONG PacketsSent;
  ULONG BadData;
  ULONG Overruns;
  ULONG UnknownTypesReceived;
  ULONG Reconfigurations;
  struct timeval LastStart;
};
</syntaxhighlight>

; PacketsReceived
: Number of packets that this unit has received.
; PacketsSent
: Number of packets that this unit has sent.
; BadData
: Number of bad packets received (i.e., hardware CRC failed).
; Overruns
: Number of packets dropped due to insufficient resources available in the network interface.
; UnknownTypeReceived
: Number of packets received that had no pending read command with the appropriate packet type.
; Reconfigurations
: Number of network reconfigurations since this unit was last configured.
; LastStart
: The time when this unit last went on-line.

returns any special statistics kept by a particular driver. Each new wire type will have a set of documented, required statistics for that wire type and a standard set of optional statistics for that wire type (optional because they might not be available from all hardware). The data returned by S2_GETSPECIALSTATS will require wire-specific interpretation. See &lt;devices/sana2specialstats.h&gt; on page devices<sub>s</sub>ana2specialstats<sub>p</sub>age for currently defined special statistics. The statistics are returned in the following structures:

<syntaxhighlight lang="C" line>
struct Sana2SpecialStatRecord
{
  ULONG Type;
  ULONG Count;
  char * String;
};
</syntaxhighlight>

; Type
: Statistic identifier.
; Count
: Statistic itself.
; String
: An identifying, null-terminated string for the statistic. Should be plain ASCII with no formatting characters.

<syntaxhighlight lang="C" line>
struct Sana2SpecialStatHeader
{
  ULONG RecordCountMax;
  ULONG RecordCountSupplied;
  struct Sana2SpecialStatRecord[RecordCountMax];
};
</syntaxhighlight>

; RecordCountMax
: There is space for this many records into which statistics may be placed.
; RecordCountSupplied
: Number of statistic records supplied.

is not, strictly speaking, a statistical function. It is a request to read any packet of a type for which there is no outstanding CMD_READ. S2_READORPHAN might be used in the same manner as many statistics, though, such as to determine what packet types are causing overruns, etc.

== Configuration ==

The device driver needs to configure the hardware before using it. The driver user must know some network hardware parameters (hardware address and MTU, for example) when using it. These commands address those needs.

When a driver user is initialized, it should try to S2_CONFIGINTERFACE even though an interface can only be configured once and someone else may have done it. Before you call S2_CONFIGINTERFACE, first call S2_GETSTATIONADDRESS to determine the factory address (if any). Also provide for user-override of the factory address (that address may be optional and the user may need to override it). When S2_CONFIGINTERFACE returns, check the ios2_SrcAddr for the actual address the hardware has been configured with. This is because some hardware (or serial line standards such as PPP) always dynamically allocates an address at initialization.

Driver users will want to use S2_DEVICEQUERY to determine the MTU and other characteristics of the network. The structure returned from S2_DEVICEQUERY is defined as:

<syntaxhighlight lang="C" line>
struct Sana2DeviceQuery
{
    /*
    ** Standard information
    */
    ULONG SizeAvailable;    /* bytes available */
    ULONG SizeSupplied;     /* bytes supplied */
    LONG  DevQueryFormat;   /* this is type 0 */
    LONG  DeviceLevel;      /* this document is level 0 */

    /*
    ** Common information
    */
    UWORD AddrFieldSize;    /* address size in bits */
    ULONG MTU;              /* maximum packet data size */
    LONG  BPS;              /* line rate (bits/sec) */
    LONG  HardwareType;     /* what the wire is */
    ULONG RawMTU;           /* maximum raw packet data size */

    /*
    ** Format specific information
    */
};
</syntaxhighlight>

; SizeAvailable
: Size, in bytes, of the space available in which to place device information. This includes both size fields.
; SizeSupplied
: Size, in bytes, of the data supplied.
; DevQueryFormat
: The format defined here is format 0.
; DeviceLevel
: This spec defines level 0.
; AddrFieldSize
: The number of bits in an interface address.
; MTU
: Maximum Transmission Unit, the size, in bytes, of the maximum packet size, not including header and trailer information.
; BPS
: Best guess at the raw line rate for this network in bits per second.

; HardwareType
: Specifies the type of network hardware the driver controls.

; RawMTU
: The <tt>RawMTU</tt> member is new as of SANA-II Revision 4. Devices which do not know and support this structure member may fill in the <tt>Sana2DeviceQuery</tt> structure only up to and including the <tt>HardwareType</tt> member.

: In this context 'raw' means the number of bytes that are available for reading and writing when using the <tt>SANA2IOB_RAW</tt> flag with a <tt>CMD_READ</tt>/<tt>CMD_WRITE</tt> request on a device that supports these access methods. Currently, software developers can only make assumptions on how many bytes might comprise the 'raw' MTU by checking the <tt>Sana2DeviceQuery.HardwareType</tt> member and hoping that the driver supports raw <tt>CMD_READ</tt>/<tt>CMD_WRITE</tt> access.

: Devices which know and support the <tt>RawMTU</tt> member must fill it with a well-defined value. For Amiga Ethernet drivers, that value would be 1514, which is the standard MTU value of 1500 bytes plus the size of the Ethernet frame header, as per RFC894 (six bytes for the destination address, six bytes for the source address and two bytes for the frame type; the eight byte preamble and the terminating four byte CRC value are typically not under the control of the driver). Drivers which do not support raw read or write access must set the <tt>RawMTU</tt> member to zero.

: If the <tt>RawMTU</tt> member is not provided, all bets are off and the application software must fall back to making estimates based upon the hardware type and the raw frame types it wishes to read and write. Ultimatively, the driver itself must decide whether it can accept raw read and write commands (or has to reject them with <tt>S2ERR_NOT_SUPPORTED</tt>) and whether the raw packet size is still covered by the underlying hardware MTU (or must be rejected with <tt>S2ERR_MTU_EXCEEDED</tt>).

: A word of warning: a little testing with various Ethernet hardware drivers has revealed that the A2065 driver <tt>a2065.device</tt> does not handle the <tt>S2_DEVICEQUERY</tt> command properly if the <tt>Sana2DeviceQuery</tt> structure provided is larger than 30 bytes. In other words, the command will fail if the proposed <tt>RawMTU</tt> member is present in the query data structure to be filled in.

== On-line ==

In order to run hardware tests on an otherwise live system, the S2_OFFLINE command allows the SANA-II device driver to be "turned off" until the tests are complete and an S2_ONLINE is sent to the driver. S2_ONLINE causes the interface to re-configure and re-initialize. Any packets destined for the hardware while the device is off-line will be lost. All pending and new requests to the driver shall be returned with S2ERR_OUTOFSERVICE when a device is off-line.

All driver users must understand that any IO request may return with S2ERR_OUTOFSERVICE because the driver is off-line (any other program may call S2_OFFLINE to make it so). In such an event, the driver will usually want to wait until the unit comes back on-line (for the program which called S2_OFFLINE to call S2_ONLINE). It may do this by calling S2_ONEVENT to wait for S2EVENT_ONLINE. S2_ONEVENT allows the driver user to wait on various events.

A driver must track events, but may not distinguish between some types of events. Drivers return S2_ONEVENT with S2ERR_NOT_SUPPORTED and S2WERR_BAD_EVENT for unsupported events. One error may cause more than one event (see below). Errors which seem to have been caused by a malformed or unusual request should not generally trigger an event.<br />

{| class="wikitable"
! Event type
! Description
|-
| S2EVENT_ERROR
| Return when any error occurs.
|-
| S2EVENT_TX
| Return on any transmit error (always an error).
|-
| S2EVENT_RX
| Return on any receive error (always an error).
|-
| S2EVENT_ONLINE
| Return when unit goes on-line or return immediately if unit is already on-line (not an error).
|-
| S2EVENT_OFFLINE
| Return when unit goes off-line or return immediately if unit is already off-line (not an error.)
|-
| S2EVENT_BUFF
| Return on any buffer management function error (always an error).
|-
| S2EVENT_HARDWARE
| Return when any hardware error occurs (always an error, may be a S2EVENT_TX or S2EVENT_RX, too).
|-
| S2EVENT_SOFTWARE
| Return when any software error occurs (always an error, may be a S2EVENT_TX or S2EVENT_RX, too).
|-
| S2EVENT_CONFIGCHANGED
| Return when client-visible configuration information changes (not an error).
|-
| S2EVENT_CONNECT
| Return when the driver has successfully established a link layer connection (not an error).
|-
| S2EVENT_DISCONNECT 
| Return when the driver has closed the link layer connection previously established by the S2_CONNECT command (not an error).
|}

=== S2EVENT_CONFIGCHANGED ===

For drivers such as those which implement the <tt>S2_GETPEERADDRESS</tt> and <tt>S2_GETDNSADDRESS</tt> commands it is vital that such changes can take place and be noticed by the client software. For this purpose a new event type is introduced, to be used with the SANA-II <tt>S2_ONEVENT</tt> command, using the following definition:

<tt>#define S2EVENT_CONFIGCHANGED (1L&lt;&lt;8)</tt>

This event should be triggered whenever client-visible configuration information changes, as can be queried via the <tt>S2_DEVICEQUERY</tt>, <tt>S2_GETSTATIONADDRESS</tt>, <tt>S2_GETSPECIALSTATS</tt>, <tt>S2_GETGLOBALSTATS</tt>, <tt>S2_GETPEERADDRESS</tt> and <tt>S2_GETDNSADDRESS</tt> commands. Here is a short list of what could change:

# S2_DEVICEQUERY: <tt>AddrFieldSize</tt>, <tt>MTU</tt>, <tt>BPS</tt>
# S2_GETSTATIONADDRESS: <tt>ios2_SrcAddr</tt>
# S2_GETGLOBALSTATS: <tt>Reconfigurations</tt>, <tt>LastStart</tt>
# S2_GETPEERADDRESS: <tt>ios2_SrcAddr</tt>, <tt>ios2_DstAddr</tt>
# S2_GETDNSADDRESS: <tt>ios2_SrcAddr</tt>, <tt>ios2_DstAddr</tt>

The purpose of this event '''is not''' to post a notification whenever another byte or event counter has changed so that a monitoring program may update its display. The purpose '''is''' to convey to the client software that an important device configuration option has changed and that it is supposed to react and adapt to it. For example, a TCP/IP stack may, upon learning that a device's IP address has changed, rebuild its routing table.

Since the <tt>S2EVENT_CONFIGCHANGED</tt> event may arrive at any time and does not indicate what exactly has changed, application software should query the information it expects to change during its life time, and keep a copy of it around for later reference. When the <tt>S2EVENT_CONFIGCHANGED</tt> event arrives, it can compare the contents of the copy against the current state of affairs and act according to the differences it finds.

=== S2EVENT_CONNECT ===

This event is a counterpart to <tt>S2EVENT_ONLINE</tt>, and is associated with the <tt>S2_CONNECT</tt> command. It has the following semantics:

<tt>#define S2EVENT_CONNECT (1L&lt;&lt;9) /* Driver has opened session */</tt>

The event is to be sent when the driver has successfully established a link layer connection.

=== S2EVENT_DISCONNECT ===

This event is a counterpart to <tt>S2EVENT_OFFLINE</tt>, and is associated with the <tt>S2_DISCONNECT</tt> command. It has the following semantics:

<tt>#define S2EVENT_DISCONNECT (1L&lt;&lt;10) /* Driver has closed session */</tt>

The event is to be sent when the driver has closed the link layer connection previously established by the <tt>S2_CONNECT</tt> command.

== S2_ONLINE and S2_OFFLINE ==

The <tt>S2_CONNECT</tt> and <tt>S2_DISCONNECT</tt> commands are somewhat related to the <tt>S2_ONLINE</tt> and <tt>S2_OFFLINE</tt> commands. How this relation works out shall be explained below. Note that the following text assumes that both the <tt>S2_CONNECT</tt>/<tt>S2_DISCONNECT</tt> and <tt>S2_ONLINE</tt>/<tt>S2_OFFLINE</tt> command pairs are implemented.

<tt>S2_CONNECT</tt> implies <tt>S2_ONLINE</tt> and, if successful, may report <tt>S2EVENT_ONLINE</tt> and <tt>S2EVENT_CONNECT</tt> events. If the unit is currently disconnected, but still online, only the <tt>S2EVENT_CONNECT</tt> event shall be sent. Invoking the <tt>S2_CONNECT</tt> command on a driver which is already connected must be rejected with <tt>ios2_Req.io_Error=S2ERR_BAD_STATE</tt> and <tt>ios2_WireError=S2WERR_UNIT_CONNECTED</tt>.

<tt>S2_DISCONNECT</tt> implies <tt>S2_OFFLINE</tt> and, if successful, may report <tt>S2EVENT_OFFLINE</tt> and <tt>S2EVENT_DISCONNECT</tt> events. If the unit is currently connected and offline, then only the <tt>S2EVENT_DISCONNECT</tt> event shall be sent. Invoking the <tt>S2_DISCONNECT</tt> command on a driver which is already disconnected must be rejected with <tt>ios2_Req.io_Error=S2ERR_BAD_STATE</tt> and <tt>ios2_WireError=S2WERR_UNIT_DISCONNECTED</tt>.

<tt>S2_OFFLINE</tt> may be used after the <tt>S2_CONNECT</tt> has successfully connected the unit. In this case the driver will release control over the link layer and report the <tt>S2EVENT_OFFLINE</tt> event. The connection established using the <tt>S2_CONNECT</tt> will, however, persist.

<tt>S2_ONLINE</tt> may be used after <tt>S2_CONNECT</tt> has successfully connected the unit and the <tt>S2_OFFLINE</tt> was used. In this case the driver will again try to obtain control over the link layer and report the <tt>S2EVENT_ONLINE</tt> event if successful.

If the <tt>S2_CONNECT</tt> command was never successfully executed, then the commands <tt>S2_ONLINE</tt> and <tt>S2_OFFLINE</tt> must be rejected with <tt>ios2_Req.io_Error=S2ERR_BAD_STATE</tt> and <tt>ios2_WireError=S2WERR_UNIT_DISCONNECTED</tt>.

== Wireless (IEEE 802.11) ==

Several new commands have been added to the SANA-II API to facilitate use of IEEE 802.11 wireless network devices in client and ad-hoc modes. The new commands are split into two overlapping sets. One set needs to be implemented by drivers for Soft-MAC wireless devices, while the other is implemented for Hard-MAC devices.

{| class="wikitable"
! Soft-MAC commands
|-
| S2_SETOPTIONS
|-
| S2_SETKEY
|-
| S2_WRITEMGMT
|-
| S2_READMGMT
|}

{| class="wikitable"
! Hard-MAC commands
|-
| S2_SETOPTIONS
|-
| S2_SETKEY
|-
| S2_GETNETWORKS
|-
| S2_GETNETWORKINFO
|-
| S2_GETSIGNALQUALITY
|}

=== sana2wireless.h ===

<syntaxhighlight lang="C" line>
#ifndef DEVICES_SANA2WIRELESS_H
#define DEVICES_SANA2WIRELESS_H

#include <exec/types.h>
#include <utility/tagitem.h>


/* Constants */
/* ========= */

/* Tags to get and set information */

#define S2INFO_SSID           (TAG_USER + 0)
#define S2INFO_BSSID          (TAG_USER + 1)
#define S2INFO_AuthTypes      (TAG_USER + 2)
#define S2INFO_AssocID        (TAG_USER + 3)
#define S2INFO_Encryption     (TAG_USER + 4)
#define S2INFO_PortType       (TAG_USER + 5)
#define S2INFO_BeaconInterval (TAG_USER + 6)
#define S2INFO_Channel        (TAG_USER + 7)
#define S2INFO_Signal         (TAG_USER + 8)
#define S2INFO_Noise          (TAG_USER + 9)
#define S2INFO_Capabilities   (TAG_USER + 10)
#define S2INFO_InfoElements   (TAG_USER + 11)
#define S2INFO_WPAInfo        (TAG_USER + 12)
#define S2INFO_Band           (TAG_USER + 13)
#define S2INFO_DefaultKeyNo   (TAG_USER + 14)

/* Wireless Commands */

#define S2_GETSIGNALQUALITY 0xc010
#define S2_GETNETWORKS      0xc011
#define S2_SETOPTIONS       0xc012
#define S2_SETKEY           0xc013
#define S2_GETNETWORKINFO   0xc014
#define S2_READMGMT         0xc015
#define S2_WRITEMGMT        0xc016
#define S2_GETRADIOBANDS    0xc017

/* Encryption types */

#define S2ENC_NONE 0
#define S2ENC_WEP  1
#define S2ENC_TKIP 2
#define S2ENC_CCMP 3

/* Radio modes */

#define S2BAND_A 0
#define S2BAND_B 1
#define S2BAND_G 2
#define S2BAND_N 3

/* Network topologies */

#define S2PORT_MANAGED 7
#define S2PORT_ADHOC   8


/* Structures */
/* ========== */

/* Structure for returning signal quality */

struct Sana2SignalQuality
{
   LONG SignalLevel;   /* signal level in dBm */
   LONG NoiseLevel;   /* noise level in dBm */
};

#endif
</syntaxhighlight>

= Driver requirements =

This section is an attempt to clarify parts of the specification and to lay down a few rules that every SANA-II driver must follow.

* A driver that does not use a broadcast medium, such as Ethernet, must not implement the <tt>S2_BROADCAST</tt> command. Likewise, if no multicast mechanism is supported, the <tt>S2_MULTICAST</tt> must not be implemented either.

* SANA-II standard commands which the driver does not implement must be rejected with the <tt>IOERR_NOCMD</tt> error code. Commands that are implemented, but which cannot perform the requested services, must be rejected with the <tt>S2ERR_NOT_SUPPORTED</tt> error code. The difference between the two cases is in when the decision is made whether a command can be handled or not. Which commands should return <tt>IOERR_NOCMD</tt> is decided upon at the time the driver is designed and implemented. At this stage the implementer knows for sure which capabilities the driver will have and which it will not have. Commands which the driver will never be able to execute will be made to return the <tt>IOERR_NOCMD</tt> error code. If the decision whether a command can be executed is made only at run time, by evaluating the conditions under which a command can be executed, then the error code <tt>S2ERR_NOT_SUPPORTED</tt> should be returned in case of failure.

* It must be possible to open the driver with an ordinary <tt>struct IOStdReq</tt>. This is necessary for the NewStyleDevices query command to work. In the command dispatcher, the driver must verify that all SANA-II commands are invoked with a proper size <tt>struct IOSana2Req</tt> I/O request. If the I/O request is shorter (as can be verified by looking at the embedded Message's <tt>mn_Length</tt> member), the command must be rejected with <tt>ios2_Req.io_Error=IOERR_BADLENGTH</tt>.

* A driver must implement the NewStyleDevices <tt>NSCMD_DEVICEQUERY</tt> command, in conformance with the NSD specification 1.6 or newer. The motivation for this is to have a mechanism available for probing the capabilities of the driver, and the supported command set can provide for vital clues. In this context, the absence of the <tt>S2_BROADCAST</tt> command would suggest that the driver cannot send or receive broadcast messages.

* A driver that does not allow its station address to be set with the <tt>S2_CONFIGINTERFACE</tt> command may silently ignore the command (returning it without setting an error condition) and even pretend that it can be configured more than once.

* In response to the <tt>S2_DEVICEQUERY</tt>, <tt>S2_GETSPECIALSTATS</tt> and <tt>S2_GETGLOBALSTATS</tt> commands a driver may return information that is not entirely correct if it has to go online before it can provide for the correct data. For example, the maximum transmission unit for PPP is a number in the range of [1..1500] which is negotiated during the protocol configuration process. It is unlikely that numbers greater than 1500 will be used, yet it is still not impossible. Since the actual number will be known only after the driver has configured the protocol, the MTU value returned before the session was opened can differ from the MTU value valid after it has been opened. A driver should therefore attempt to return 'safe' defaults in place of information that is unavailable at the time it is queried. The 'safe' values shall be set up to allow the driver to work even if the protocol stack is not aware of later changes to those values. Beware of zero-length buffer sizes or time intervals that may cause client software to perform zero-length memory allocations or divisions by zero.

* SANA-II assigns packet type numbers according to the underlying transport media. For example, Ethernet uses packet type 2048 for IP frames. No such packet type definition exists for PPP yet, which is why this standard proposes to assign packet type 31 for IP packets transmitted via PPP. To simplify client software configuration, drivers may treat packet type 2048 as equivalent to the packet number associated with IP frames. This association is permitted only if it does not introduce ambiguity. For example, this association would not be permitted if the driver would receive and transmit IP packets in two different frame types or if the driver already associates packet type 2048 with non-IP packets.

= Driver Installation =

The standard system location for SANA-II network device driver is in a directory called &quot;Networks&quot; which exists in the &quot;DEVS:&quot; directory.

Example:

<blockquote>
DEVS:Networks/eth3com.device
</blockquote>

This is the official location for the drivers. It may be necessary for your install program/script to create this directory if it doesn't exist in a user's system.

= Unresolved Issues =

* Unfortunately, it isn't possible to completely isolate network protocols from the hardware they run on. Hardware types and addressing both remain somewhat hardware-dependent in spite of our efforts. See the "Packet Type" section for an explanation of how packet types are handled and why protocols cannot be isolated from them. See the "Addressing" section for an explanation of how addressing is handled any why protocols cannot be isolated from it.

* Additionally, there are at least two cases where a hardware type has multiple framing methods in use (ethernet/802.3 and arcnet/(Novell) "ARCNET Packet Header Definition Standard"). In both cases, software which must interoperate with other platforms on this hardware may need to be aware of the distinctions and may have to do extra processing in order to use the appropriate frame type. See the sections on "Ethernet Packet Types" and on "ARCNET frames" for more details.

* Protocols like PPP can provide both for IPv4 and IPv6 addresses that should be used by the local client, the peer or any of the domain name servers. While the two addressing families are interoperable, there is a problem in how the driver should report them. Can you assume 128 bit addresses and encapsulate 32 bit addresses in them? If so, how do you make sure that the address format is unambiguous?

* Currently, only the device's hardware type provides a clue as to what packet type responds to which protocol transported via the link. For Ethernet, IP packets are encapsulated in type 2048 frames, PPP encapsulates IP packets in type 31 frames, Arcnet can use type 240 or 212. Matching a protocol with a frame type is not an easy process which could be handled more elegantly.

* How to extend the <tt>Sana2DeviceQuery</tt> structure in the future? The current layout separates standard, common and format specific information, but there is no hint as to where which each section starts and where the next begins. Now that there is a proposal to add a new field to the common section, how would you add fields to the format specific section?

= SANA-II network device driver Autodocs =

== AbortIO ==

<pre>
   NAME
        AbortIO -- Remove an existing device request.

   SYNOPSIS
        error = AbortIO(Sana2Req)
        D0              A1

        LONG AbortIO(struct IOSana2Req *);

   FUNCTION
        This is an exec.library call.

        This function aborts an ioRequest. If the request is active, it may or
        may not be aborted. If the request is queued it is removed. The
        request will be returned in the same way as if it had normally
        completed.  You must WaitIO() after AbortIO() for the request to
        return.

   INPUTS
        Sana2Req        - Sana2Req to be aborted.

   RESULTS
        error           - Zero if the request was aborted, non-zero otherwise.
                          io_Error in Sana2Req will be set to IOERR_ABORTED
                          if it was aborted.

   SEE ALSO
        exec.library/AbortIO(), exec.library/WaitIO()
</pre>

== CloseDevice ==

<pre>
   NAME
        CloseDevice -- Close the device.

   SYNOPSIS
        CloseDevice(Sana2Req)
                    A1

        void CloseDevice(struct IOSana2Req *);

   FUNCTION
        This function is called by exec.library CloseDevice().

        This function performs whatever cleanup is required at device closes.

        Note that all IORequests MUST be complete before closing. If any are
        pending, your program must AbortIO() then WaitIO() each outstanding
        IORequest to complete them.

   INPUTS
        Sana2Req        - Pointer to IOSana2Req initialized by OpenDevice().

   SEE ALSO
        exec.library/CloseDevice(), exec.library/OpenDevice()
</pre>

== CMD_CLEAR ==

<pre>
   NAME
        CMD_CLEAR -- Clear internal network interface read buffers.

   FUNCTION
        There are no device internal buffers, so CMD_CLEAR does not apply to
        this class of device.

   IO REQUEST
        ios2_Command    - CMD_CLEAR.

   RESULTS
        ios2_Error      - IOERR_NOCMD.
</pre>

== CMD_FLUSH ==

<pre>
   NAME
        CMD_FLUSH -- Clear all queued I/O requests for the SANA-II device.

   FUNCTION
        This command aborts all I/O requests in both the read and write
        request queues of the device.  All pending I/O requests are
        returned with an error message (IOERR_ABORTED).  CMD_FLUSH does not
        affect active requests.

   IO REQUEST
        ios2_Command    - CMD_FLUSH.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
</pre>

== CMD_INVALID ==

<pre>
   NAME
        CMD_INVALID -- Return with error IOERR_NOCMD.

   FUNCTION
        This command causes device driver to reply with an error IOERR_NOCMD
        as defined in &lt;exec/errors.h&gt; indicating the command is not supported.

   IO REQUEST
        ios2_Command    - CMD_INVALID.

   RESULTS
        ios2_Error      - IOERR_NOCMD.

   BUGS
        Not known to be useful.
</pre>

== CMD_READ ==

<pre>
   NAME
        CMD_READ -- Get a packet from the network.

   FUNCTION
        Get the next packet available of the requested packet type. The data
        copied (via a call to the requestor-provided CopyToBuffer function)
        into ios2_Data is normally the Data Link Layer packet data only. If
        bit SANA2IOB_RAW is set in ios2_Flags, then the entire physical frame
        will be returned.

        Unlike most Exec devices, SANA-II device drivers do not have internal
        buffers.  If you wish to read data from a SANA-II device you should
        have multiple CMD_READ requests pending at any given time.  The
        functions provided by you the requestor will be used for any incoming
        packets of the type you've requested.  If no read requests are
        outstanding for a type which comes in and no read_orphan requests are
        outstanding, the packet will be lost.

   IO REQUEST
        ios2_Command    - CMD_READ
        ios2_Flags      - Supported flags are:
                            SANA2IOB_RAW
                            SANA2IOB_QUICK
        ios2_PacketType - Packet type desired.
        ios2_Data       - Abstract data structure to hold packet data.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.
        ios2_Flags      - The following flags may be returned:
                            SANA2IOB_RAW
                            SANA2IOB_BCAST
                            SANA2IOB_MCAST
        ios2_SrcAddr    - Source interface address of packet.
        ios2_DstAddr    - Destination interface address of packet.
        ios2_DataLength - Length of packet data.
        ios2_Data       - Abstract data structure which packet data is
                          contained in.

   NOTES
        The driver may not directly examine or modify anything pointed to by
        ios2_Data.  It *must* use the requester-provided functions to access
        this data.

   SEE ALSO
        S2_READORPHAN, CMD_WRITE, any_protocol/CopyToBuffer
</pre>

== CMD_RESET ==

<pre>
   NAME
        CMD_RESET -- Reset the network interface to initialized state.

   FUNCTION
        Currently, SANA-II devices can only be configured once (with
        CMD_CONFIGINTERFACE) and cannot be re-configured, hence,
        CMD_RESET does not apply to this class of device.

   IO REQUEST
        ios2_Command    - CMD_RESET.

   RESULTS
        ios2_Error      - IOERR_NOCMD.
</pre>

== CMD_START ==

<pre>
   NAME
        CMD_START -- Restart device operation.

   FUNCTION
        There is no way for the driver to keep queuing requests without
        servicing them, so CMD_STOP does not apply to this class of device.
        S2_OFFLINE and S2_ONLINE do perform a similar function to CMD_STOP
        and CMD_START

   IO REQUEST
        ios2_Command    - CMD_START.

   RESULTS
        ios2_Error      - IOERR_NOCMD.

   SEE ALSO
        S2_ONLINE, S2_OFFLINE
</pre>

== CMD_STOP ==

<pre>
   NAME
        CMD_STOP -- Pause device operation.

   FUNCTION
        There is no way for the driver to keep queuing requests without
        servicing them, so CMD_STOP does not apply to this class of device.
        S2_OFFLINE and S2_ONLINE do perform a similar function to CMD_STOP
        and CMD_START

   IO REQUEST
        ios2_Command    - CMD_STOP.

   RESULTS
        ios2_Error      - IOERR_NOCMD.

   NOTES

   SEE ALSO
        S2_ONLINE, S2_OFFLINE
</pre>

== CMD_UPDATE ==

<pre>
   NAME
        CMD_UPDATE -- Force packets out to device.

   FUNCTION
        Since there are no device internal buffers, CMD_UPDATE does not
        apply to this class of device.

   IO REQUEST
        ios2_Command    - CMD_UPDATE.

   RESULTS
        ios2_Error      - IOERR_NOCMD.
</pre>

== CMD_WRITE ==

<pre>
   NAME
        CMD_WRITE -- Send packet to the network.

   FUNCTION
        This command causes the packet to be sent to the specified network
        interface. Normally, appropriate packet header and trailer information
        will be added to the packet data when it is sent.  If bit SANA2IOB_RAW
        is set in io_Flags, then the ios2_Data is assumed to contain an entire
        physical frame and will be sent (copied to the wire via
        CopyFromBuffer() unmodified.

        Note that the device should not check to see if the destination
        address is on the local hardware.  Network protocols should realize
        that the packet has a local destination long before it gets to a
        SANA-II driver.

   IO REQUEST
        ios2_Command    - CMD_WRITE.
        ios2_Flags      - Supported flags are:
                            SANA2IOB_RAW
                            SANA2IOB_QUICK
        ios2_PacketType - Packet type to send.
        ios2_DstAddr    - Destination interface address for this packet.
        ios2_DataLength - Length of the Data to be sent.
        ios2_Data       - Abstract data structure which packet data is
                          contained in.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.

   NOTES
        The driver may not directly examine or modify anything pointed to by
        ios2_Data.  It *must* use the requester-provided functions to access
        this data.

   SEE ALSO
        CMD_READ, S2_BROADCAST, S2_MULTICAST, any_protocol/CopyFromBuffer
</pre>

== OpenDevice ==

<pre>
   NAME
        Open -- Request an opening of the network device.

   SYNOPSIS
        error = OpenDevice(unit, IOSana2Req, flags)
        D0                 D0    A1          D1

        BYTE OpenDevice(ULONG, struct IOSana2Req *, ULONG);

   FUNCTION
        This function is called by exec.library OpenDevice().

        This function performs whatever initialization is required per
        device open and initializes the Sana2Req for use by the
        device.

   INPUTS
        unit            - Device unit to open.
        Sana2Req        - Pointer to IOSana2Req structure to be initialized by
                          the sana2.device.
        flags           - Supported flags are:
                                SANA2OPB_MINE
                                SANA2OPB_PROM
        ios2_BufferManagement   - A pointer to a tag list containing
                                  pointers to buffer management functions.

   RESULTS
        error           - same as io_Error
        io_Error        - Zero if successful; non-zero otherwise.
        io_Device       - A pointer to whatever device will handle the calls
                          for this unit.  This pointer may be different
                          depending on what unit is requested.
        ios2_BufferManagement   - A pointer to device internal information
                                  used to call buffer management functions.

   NOTES
        A SANA-II device must reject all open requests with a request
        structure that is too short, e.g. an IOStdReq. A simple check of the
        mn_Length field in the Message part of the request is needed to make
        sure that a device does not dereference invalid data due to a wrong
        device configuration.

        A SANA-II device may open if no buffer management tags are provided to
        make the configuration process and obtaining statistics easier. Buffer
        management tags with a NULL value must be treated as not specified.
        The device shall fail requests gracefully depending on the missing
        tags in this case. Any malfunction is not acceptable.

   SEE ALSO
        exec.library/OpenDevice(), exec.library/CloseDevice()
</pre>

== S2_ADDMULTICASTADDRESS ==

<pre>
   NAME
        S2_ADDMULTICASTADDRESS -- Enable an interface multicast address.

   FUNCTION
        This command causes the device driver to enable multicast packet
        reception for the requested address.

   IO REQUEST
        ios2_Command    - S2_ADDMULTICASTADDRESS.
        ios2_SrcAddr    - Multicast address to enable.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.

   NOTES
        Multicast addresses are added globally -- anyone using the device
        may receive packets as a result of any multicast address which has
        been added for the device.

        Since multicast addresses are not &quot;bound&quot; to a particular packet type,
        each enabled multicast address has an &quot;enabled&quot; count associated with
        it so that if two protocols add the same multicast address and later
        one removes it, it is still enabled until the second removes it.

   SEE ALSO
        S2_MULTICAST, S2_DELMULTICASTADDRESS
</pre>

== S2_BROADCAST ==

<pre>
   NAME
        S2_BROADCAST -- Broadcast a packet on network.

   FUNCTION
        This command works the same as CMD_WRITE except that it also performs
        whatever special processing of the packet is required to do a
        broadcast send. The actual broadcast mechanism is neccessarily
        network/interface/device specific.

   IO REQUEST
        ios2_Command    - S2_BROADCAST.
        ios2_Flags      - Supported flags are:
                                SANA2IOB_RAW
                                SANA2IOB_QUICK
        ios2_PacketType - Packet type to send.
        ios2_DataLength - Length of the Data to be sent.
        ios2_Data       - Abstract data structure which packet data is
                          contained in.

   RESULTS
        ios2_DstAddr    - The contents of this field are to be
                          considered trash upon return of the IOReq.
        ios2_Error      - Zero if successful; non-zero otherwise.
                          This command can fail for many reasons and
                          is not supported by all networks and/or
                          network interfaces.
        ios2_WireError  - More specific error number.

   NOTES
        The DstAddr field may be trashed by the driver because this function
        may be implemented by filling DstAddr with a broadcast address and
        internally calling CMD_WRITE.

   SEE ALSO
        CMD_WRITE, S2_MULTICAST
</pre>

== S2_CONFIGINTERFACE ==

<pre>
   NAME
        S2_CONFIGINTERFACE -- Configure the network interface.

   FUNCTION
        This command causes the device driver to initialize the interface
        hardware and to set the network interface address to the address in
        ios2_SrcAddr. This command can only be executed once and, if
        successful, will leave the driver and network interface fully
        operational and the network interface in ios2_SrcAddr.

        To set the interface address to the factory address, the network
        management software must use GetStationAddress first and then call
        ConfigInterface with the result. If there is no factory address then
        the network software must pick an address to use.

        Until this command is executed the device will not listen for any
        packets on the hardware.

   IO REQUEST
        ios2_Command    - S2_CONFIGINTERFACE.
        ios2_Flags      - Supported flags are:
                                SANA2IOB_QUICK
        ios2_SrcAddr    - Address for this interface.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.
        ios2_SrcAddr    - Address of this interface as configured.

   NOTES
        Some networks have the interfaces choose a currently unused interface
        address each time the interface is initialized. The caller must check
        ios2_SrcAddr for the actual interface address after configuring the
        interface.

   SEE ALSO
        S2_GETSTATIONADDRESS
</pre>

== S2_CONNECT ==

The driver model specified by the SANA-II standard really only covers networking hardware well. Software-only drivers, such as for dial-up networking, are, well, somehow mentioned in the standard, but they don't receive much attention. In particular, this means that a networking driver is assumed to be practically always attached to its link layer and no provisions exist to specify what kind of link layer that might be and how it might be accessed. This small oversight can probably be explained by the fact that at the time the SANA-II standard was adopted, dial-up networking had not yet gained the prominence it has today.

To bridge this gap, I propose a new command which will make a driver connect to its link layer and go online, which uses the following data structure:

<syntaxhighlight lang="C" line>
struct Sana2Connection
{
   ULONG          s2c_Size;
   struct MinList s2c_Options;
   struct Hook    s2c_ErrorHook;
   struct Hook    s2c_ConnectHook;
   struct Hook    s2c_DisconnectHook;
   STRPTR         s2c_Login;
   STRPTR         s2c_Password;
};
</syntaxhighlight>

The individual structure members have the following purposes:

<tt>s2c_Size</tt>

The size of the entire data structure is stored here. This value '''must''' be &gt;= 84. Smaller values '''must''' be rejected with <tt>ios2_Req.io_Error</tt>=<tt>IOERR_BADLENGTH</tt>. The purpose of <tt>s2c_Size</tt> is to allow for future expansion during which the structure may grow in size.

<tt>s2c_Options</tt>

This list contains options, to be used during the connection process. Each node has the following format:

<syntaxhighlight lang="C" line>
struct Sana2ConnectionOption
{
   struct MinNode s2co_MinNode;
   STRPTR         s2co_Name;
   STRPTR         s2co_Value;
};
</syntaxhighlight>

The <tt>s2co_Name</tt> and <tt>s2co_Value</tt> entries point to <tt>NUL</tt>-terminated strings, which contain the name and the value of a parameter. ''Note that for numeric values, the respective number will be encoded in a text string.'' A number of parameters are reserved, which are [[#s2c_options|listed later]] in this text.

<tt>s2c_ErrorHook</tt>

This hook is called whenever an error message is to be reported during the connection/disconnection process. The hook function is invoked using the following parameters:

<pre>error_hook_func(hook,reserved,message);

VOID error_hook_func(struct Hook *hook,APTR reserved,
                     STRPTR message);</pre>
The <tt>reserved</tt> parameter '''must''' be <tt>NULL</tt>. The <tt>message</tt> parameter points to a <tt>NUL</tt>-terminated string. It '''must not''' be <tt>NULL</tt>.

Because the hook function may have to allocate memory, it '''must not''' be called from interrupt code.

<tt>s2c_ConnectHook</tt>

This hook is called when the link level device has been set up, but further initializations are necessary, such as telling a modem to dial out. The hook function is invoked with the following parameters:

<pre>
success = connect_hook_func(hook,reserved,s2cm);

BOOL connect_hook_func(struct Hook *hook,APTR reserved,
                       struct Sana2ConnectionMessage *s2cm);
</pre>

The <tt>reserved</tt> parameter '''must''' be <tt>NULL</tt>. The <tt>s2cm</tt> parameter points to a data structure, as follows:

<syntaxhighlight lang="C" line>
struct Sana2ConnectionMessage
{
   ULONG                    s2cm_Size;
   struct Sana2Connection * s2cm_Connection;
   struct IORequest *       s2cm_Request[2];
   LONG                     s2cm_RequestType;
};
</syntaxhighlight>

In this structure, the members have the following purposes:

<tt>s2cm_Size</tt>

The size of this data structure; it '''must''' be at least 20 bytes in size. The purpose of <tt>s2cm_Size</tt> is to allow for future expansion, which may cause the size of this structure to grow.

<tt>s2cm_Connection</tt>

This points back to the <tt>Sana2Connection</tt> structure which the hook that was invoked with the <tt>Sana2ConnectionMessage</tt> is embedded in.

<tt>s2cm_Request</tt>

Here you will find two I/O requests which can be used for reading and writing data to the link layer. These pointers '''must not''' be <tt>NULL</tt> and they '''must''' refer to different I/O requests, it is not permitted to pass the same request twice.

The dialer can use these requests for communicating with the modem, but it is also permitted to clone these requests by creating new I/O requests of the same size, copying the original contents and filling in different reply ports.

There is a danger in that the hook code may not receive the right kind of I/O request, which is why the <tt>s2cm_RequestType</tt> field identifies the kind of device the requests were created for.

When the hook function returns, it '''must''' make sure that none of the I/O requests are still pending, i.e. asynchronous I/O '''must''' have been stopped.

<tt>s2cm_RequestType</tt>

This identifies the type of device the I/O requests passed in <tt>s2cm_Request</tt> were created for. Possible values for this entry come from the New Style Device specification, e.g. <tt>NSDEVTYPE_SERIAL</tt> for a <tt>serial.device</tt>-like device or <tt>NSDEVTYPE_SANA2</tt> for a networking driver.

The hook function '''must''' return <tt>TRUE</tt> if the connection could be established, and <tt>FALSE</tt> otherwise. Note that it is ''not'' sufficient to just return <tt>FALSE</tt> in case of failure. Your code '''must''' have called the <tt>s2c_ErrorHook</tt> with an explanation why things went wrong first.

<tt>s2c_DisconnectHook</tt>

This hook is called by <tt>S2_CONNECT</tt> when the connection could not be established (the <tt>s2c_ConnectHook</tt> returned <tt>FALSE</tt>), or by <tt>S2_DISCONNECT</tt>, shortly before the link level device is to be closed. The hook function is invoked with the following parameters:

<pre>
disconnect_hook_func(hook,reserved,s2cm);

VOID disconnect_hook_func(struct Hook *hook,APTR reserved,
                          struct Sana2ConnectionMessage *s2cm);
</pre>

The <tt>reserved</tt> parameter '''must''' be <tt>NULL</tt>. The <tt>s2cm</tt> parameter points to a data structure, as was described for the <tt>s2c_ConnectHook</tt>.

<tt>s2c_Login</tt>

<tt>s2c_Password</tt>

The purpose of these entries is to transport the authentication information the protocol may require. These entries are either <tt>NULL</tt> or contain pointers to <tt>NUL</tt>-terminated strings. If <tt>s2c_Login</tt> is <tt>NULL</tt>, then both login and password '''must''' be assumed to be empty. If <tt>s2c_Login</tt> is not <tt>NULL</tt> and <tt>s2c_Password</tt> is <tt>NULL</tt>, then the password '''must''' be assumed to be empty.

The list of options in <tt>s2c_Options</tt> supplies the necessary information on how the driver is to connect to the link layer. Each node contains an option, which bears a name and contains a value. This pair is what I call a ''parameter''. A number of parameter names are reserved, as will be listed below:

<tt>ppp</tt>.async.device

Name of device driver to use for asynchronous PPP.

Example: <tt>serial.device</tt>

<tt>ppp</tt>.async.unit

Device unit number to use for asynchronous PPP.

Example: <tt>0</tt>

<tt>ppp</tt>.async.speed

Transmission speed to use for asynchronous PPP in bits per second.

Example: <tt>115200</tt>

<tt>ppp.async.buffersize</tt>

Receive buffer size for asynchronous PPP.

Example: <tt>50000</tt>

<tt>ppp.async.checkcarrier</tt>

Whether the carrier signal of the link layer should be tested or not. This can be either 0 (do not test) or 1 (test the carrier signal).

Example: <tt>1</tt>

<tt>ppp.async.rtscts</tt>

Whether or not hardware handshaking should be used by the link layer. This can be either <tt>0</tt> (off) or <tt>1</tt> (on).

Example: <tt>1</tt>

<tt>ppp.async.shared</tt>

Whether or not the link layer device should be opened in shared mode. This can be either <tt>0</tt> (off) or <tt>1</tt> (on).

Example: <tt>0</tt>

<tt>ppp.async.nullmodem</tt>

Whether or not the link layer is a direct connection, such as a nullmodem. This can be either <tt>0</tt> (off) or <tt>1</tt> (on).

Example: <tt>0</tt>

<tt>ppp.async.eof</tt>

Whether or not the underlying serial device driver's 'EOF mode' should be enabled. This can be either <tt>0</tt> (off) or <tt>1</tt> (on).

Example: <tt>0</tt>

<tt>ppp.async.readrequests</tt>

Number of read requests to be used for asynchronous PPP.

Example: <tt>16</tt>

<tt>ppp.async.writerequests</tt>

Number of write requests to be used for asynchronous PPP.

Example: <tt>16</tt>

<tt>ppp.async.accm</tt>

Asynchronous control character map, expressed as a hexadecimal value.

Example: <tt>$000A0000</tt>

<tt>ppp.async.pfc</tt>

Whether or not protocol field compression should be used. This can be either <tt>0</tt> (off) or <tt>1</tt> (on).

Example: <tt>1</tt>

<tt>ppp.async.aacfc</tt>

Whether or not address and control field compression should be used. This can be either <tt>0</tt> (off) or <tt>1</tt> (on).

Example: <tt>1</tt>

<tt>ppp.async.vjhc</tt>

Whether or not Van Jacobson header compression should be used. This can be either <tt>0</tt> (off) or <tt>1</tt> (on).

Example: <tt>1</tt>

<tt>ppp.async.ignorefcs</tt>

Whether or not frame check sequences should be ignored upon reception. This can be either <tt>0</tt> (off) or <tt>1</tt> (on).

Example: <tt>0</tt>

<tt>ppp.async.initialize</tt>

The modem initialization command, with embedded control sequences, if possible.

Example: <tt>AT\r</tt>

<tt>ppp.async.dial</tt>

The modem dial command, with embedded control sequences, if possible.

Example: <tt>ATD12345\r</tt>

<tt>ppp.async.dialtimeout</tt>

The dial timeout, i.e. the number of seconds to wait after the dial command has been sent for the modem to establish a connection.

Example: <tt>60</tt>

<tt>ppp.async.hangup</tt>

The modem hangup command, with embedded control sequences, if possible.

Example: <tt>ATH0\r</tt>

<tt>ppp.idletimeout</tt>

Number of seconds the local host may remain idle, i.e. send no data to the peer, before a watchdog timeout elapses and proceeds to verify that the line is still operational.

Example: <tt>30</tt>

<tt>ppp.localaddress</tt>

The IP address to assign to the local host, as part of the PPP negotiation process. This must be given in dotted decimal notation (RFC1700).

Example: <tt>1.2.3.4</tt>

<tt>ppp.remoteaddress</tt>

The IP address to assume for the peer, as part of the PPP negotiation process. This must be given in dotted decimal notation (RFC1700).

Example: <tt>1.2.3.4</tt>

<tt>ppp.dns1address</tt>

The IP address to be used by the primary domain name server, as part of the PPP negotiation process. This must be given in dotted decimal notation (RFC1700).

Example: <tt>1.2.3.4</tt>

<tt>ppp.dns2address</tt>

The IP address to be used by the secondary domain name server, as part of the PPP negotiation process. This must be given in dotted decimal notation (RFC1700).

Example: <tt>1.2.3.4</tt>

<tt>ppp.maxfail</tt>

The maximum number of negative configuration acknowledgements to be sent before the PPP negotiation process switches to reject those options.

Example: <tt>5</tt>

<tt>ppp.maxterm</tt>

The maximum number of termination requests to be sent before the respective PPP network or link protocol gives up.

Example: <tt>2</tt>

<tt>ppp.maxconfig</tt>

The maximum number of configuration requests to be sent before the respective PPP network or link protocol gives up.

Example: <tt>10</tt>

<tt>ppp.timeout</tt>

The number of seconds that have to pass before the respective PPP network or link protocol will retry to do whatever didn't work during the last attempt.

Example: <tt>3</tt>

<tt>ppp.mtu</tt>

The maximum transmission unit to use.

Example: <tt>1500</tt>

<tt>ppp.peeridletimeout</tt>

Number of seconds the peer may remain idle, i.e. send no data to the local host, before a watchdog timeout elapses and proceeds to verify that the line is still operational.

Example: <tt>30</tt>

<tt>ppp.rejectpap</tt>

Whether or not the Password Authentication Protocol should be accepted, if offered by the peer. This can be either <tt>0</tt> (accept) or <tt>1</tt> (reject).

Example: <tt>0</tt>

<tt>ppp.sendid</tt>

A flag which controls whether the local host should send LCP identification packets or not. This can be either <tt>0</tt> (off) or <tt>1</tt> (on).

Example: <tt>0</tt>

<tt>ppp.pap.timeout</tt>

The Password Authentication Protocol requires that the server answers to the client's request to log in. The server may be unable to respond immediately, which means that the client will have to repeat its request. A short delay should separate each request sent, such as three seconds.

Example: <tt>3</tt>

<tt>ppp.pap.retry</tt>

If the client does not manage to authenticate with the server immediately, it may resend the authentication request several times. But the attempts have to stop eventually, such as after having resent the message ten times.

Example: <tt>10</tt>

<tt>ppp.dummyremoteaddress</tt>

Whether or not the PPP driver should make up an IP address if the peer refuses to state its own IP address.

Example: <tt>1</tt>

<tt>logfile</tt>

The name of a log file to create. If the file already exists, then new data should be appended to it.

Example: <tt>t:logfile</tt>

<tt>logoptions</tt>

A list of options which control what exactly should be logged.

<tt>ppp.ethernet.device</tt>

Name of device driver to use for PPPoE (PPP over Ethernet).

Example: <tt>a2065.device</tt>

<tt>ppp.ethernet.unit</tt>

Device unit number to use for PPPoE.

Example: <tt>0</tt>

<tt>ppp.ethernet.raw</tt>

Whether or not raw link layer frames should be constructed for transmission or not. This can be either <tt>0</tt> (off) or <tt>1</tt> (on).

Example: <tt>1</tt>

<tt>ppp.ethernet.bypass</tt>

Whether or not the IP packet transmission and reception should bypass several copying steps. This can be either <tt>0</tt> (off) or <tt>1</tt> (on).

Example: <tt>1</tt>

<tt>ppp.ethernet.readpackets</tt>

The number of read requests to queue for the link layer.

Example: <tt>16</tt>

<tt>ppp.ethernet.writepackets</tt>

The number of write requests to queue for the link layer.

Example: <tt>16</tt>

<tt>ppp.ethernet.service</tt>

The name of the PPPoE service to request.

Example: <tt>?</tt>

<tt>ppp.ethernet.ac</tt>

The name of the PPPoE access concentrator to request.

Example: <tt>?</tt>

<tt>ppp.ethernet.connecttimeout</tt>

The number of seconds to wait for the PPPoE server to allow a session to be opened.

Example: <tt>3</tt>

While this list of parameters may suggest that the command described above can be used solely with the PPP protocol, do not let that put you off. This list is merely the starting point, but it is not set in stone that it cannot be extended.

The names of the parameters are not case sensitive. As the names suggest, the name space itself is hierarchic in construction, i.e. everything related to the PPP protocol bears a name starting with the letters 'ppp' with the dot '.' separating the individual items. By this rule, ppp.async refers to options that concern asynchronous PPP and ppp.ethernet to options that concern PPP over Ethernet wire.

To add your own parameter, register it with the maintainer of the SANA-II standard or prefix its name with the letters 'x-'. For example, to use your own kind of 'ppp.ethernet.connecttimeout' parameter, change the name of the last component like this: ppp.ethernet.x-connecttimeout. No officially-registered parameter will ever begin with the prefix 'x-'.

The command should work as follows:

# The client must set up the <tt>Sana2Connection</tt> data structure, initialize the <tt>s2c_Size</tt>, <tt>s2c_Options</tt>, <tt>s2c_ErrorHook</tt>, <tt>s2c_ConnectHook</tt>, <tt>s2c_DisconnectHook</tt>, <tt>s2c_Login</tt> and <tt>s2c_Password</tt> fields.
# The connection options must be filled in, which means that nodes containing the respective information must be stored in the <tt>s2c_Options</tt> list. The client must make sure that the syntax of the parameters conforms to the specifications described above.
# A pointer to the <tt>Sana2Connection</tt> data structure is placed in the <tt>ios2_Data</tt> member of an <tt>IOSana2Req</tt>, the command is set to <tt>S2_CONNECT</tt> and the request is sent via <tt>DoIO()</tt> or <tt>SendIO()</tt>.
# The driver receives the request and begins to examine the contents of the <tt>s2c_Options</tt> list, as provided in the data structure pointed to by the <tt>ios2_Data</tt> member of the request. Unknown options are ignored, options whose values do not conform to the syntax specification are rejected; this is done by calling the <tt>s2c_ErrorHook</tt> with an error message referring to the option in question and by returning the <tt>IOSana2Req</tt> with an error code of <tt>S2ERR_BAD_ARGUMENT</tt> and wire error code of <tt>S2WERR_INVALID_OPTION</tt>.
# If the options are all in good order, the driver proceeds to verify that all mandatory options are provided. If this is not the case, the <tt>s2c_ErrorHook</tt> is called with an error message referring to the option in question and the <tt>IOSana2Req</tt> is returned with an error code of <tt>S2ERR_BAD_ARGUMENT</tt> and wire error code of <tt>S2WERR_MISSING_OPTION</tt>.
# The driver proceeds to do its local initialization, which involves opening the link layer device, etc. If this initialization fails, the <tt>s2c_ErrorHook</tt> is called with an error message referring to the problem and the <tt>IOSana2Req</tt> is returned with an appropriate error code.
# When the initialization has finished, the driver may invoke the <tt>s2c_ConnectHook</tt> callback. Some drivers may require this, such as asynchronous PPP, some may not, such as PPPoE. The purpose of the hook function is to give the client a chance to perform modem initializations and connect to the peer. If the <tt>s2c_ConnectHook</tt> cannot perform its duties, it has to invoke the <tt>s2c_ErrorHook</tt> with an error message and eventually return <tt>FALSE</tt>. If <tt>FALSE</tt> is returned, the driver must invoke the <tt>s2c_DisconnectHook</tt>, reverse any initializations it had made and eventually returned the <tt>IOSana2Req</tt> with an appropriate error code. If the <tt>s2c_ConnectHook</tt> returned <tt>TRUE</tt>, then the driver must proceed with the actions that require that the link layer is operational. A protocol negotiation may follow, which, if successful, will make the driver return the <tt>IOSana2Req</tt> with an error code of zero, indicating success. If successful, the SANA-II events <tt>S2EVENT_CONNECT</tt> and <tt>S2EVENT_ONLINE</tt> must be sent.
# The command will eventually return, but the client '''must not''' release the memory allocated for the <tt>Sana2Connection</tt> structure and the option nodes in the <tt>s2c_Options</tt> list. This is because the driver may have to invoke the <tt>s2c_DisconnectHook</tt> hook due to the connection shutting down on its own accord.

The connect and disconnect hook functions '''must not''' be called from interrupt code. For each hook only a <tt>Task</tt> calling context of unknown priority must be assumed. Also, stack space is provided only to call <tt>exec.library</tt> and <tt>utility.library</tt> functions. The callback shall not place excessive data on the stack. Stack space should be considered limited.

I propose a command with the following semantics:

<pre>
   NAME
        S2_CONNECT -- Establish a link layer connection and go
            online.

   FUNCTION
        This command is for use by networking devices which require
        a special link layer device to transmit their data, such as
        an asynchronous serial line and need to know about the
        configuration parameters necessary to open the connection.

   IO REQUEST
        ios2_Command          - S2_CONNECT
        ios2_Data             - Pointer to Sana2Connection structure
        ios2_BufferManagement - Magic cookie as returned when opening the
                                driver with a struct IOSana2Req

   RESULTS
        ios2_Req.io_Error - Zero if successful; non-zero otherwise
        ios2_WireError    - More specific error number

   NOTES
        If successful, this command implies S2_ONLINE, i.e. the
        link layer is allocated and used by the driver.

        The contents of the Sana2Connection structure must be valid
        until the connection is eventually shut down. The driver will
        need to cache it, so it must not be deallocated or otherwise
        modified.

        Note that S2_ONLINE does not necessarily imply S2_CONNECT, if
        the S2_CONNECT command is listed as supported by the driver via
        NSCMD_DEVICEQUERY. If S2_CONNECT is not listed as supported,
        S2_ONLINE obviously implies connect functionality.

        S2_CONNECT/S2_DISCONNECT do not nest.

   SEE ALSO
        S2_DISCONNECT</pre>
I propose that the following command number should be assigned:

<tt>#define S2_CONNECT 0xC005</tt>

== S2_DELMULTICASTADDRESS ==

<pre>
   NAME
        S2_DELMULTICASTADDRESS -- Disable an interface multicast address.

   FUNCTION
        This command causes device driver to disable multicast packet
        reception for the requested address.

        It is an error to disable a multicast address that is not enabled.

   IO REQUEST
        ios2_Command    - S2_DELMULTICASTADDRESS
        ios2_SrcAddr    - Multicast address to disable.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.

   NOTES
        Multicast addresses are added globally -- anyone using the device
        may receive packets as a result of any multicast address which has
        been added for the device.

        Since multicast addresses are not &quot;bound&quot; to a particular packet type,
        each enabled multicast address has an &quot;enabled&quot; count associated with
        it so that if two protocols add the same multicast address and later
        one removes it, it is still enabled until the second removes it.

   SEE ALSO
        S2_ADDMULTICASTADDRESS
</pre>

== S2_DEVICEQUERY ==

<pre>
   NAME
        S2_DEVICEQUERY -- Return parameters for this network interface.

   FUNCTION
        This command causes the device driver to report information about the
        device. Up to SizeAvailable bytes of the information is copied
        into a buffer pointed to by ios2_StatData. The format of the data is
        as follows:

            struct Sana2DeviceQuery
            {
            /*
            ** Standard information
            */
                ULONG SizeAvailble; /* bytes available */
                ULONG SizeSupplied; /* bytes supplied */
                LONG  DevQueryFormat;   /* this is type 0 */
                LONG  DeviceLevel;      /* this document is level 0 */

            /*
            ** Common information
            */
                UWORD AddrFieldSize;    /* address size in bits */
                ULONG MTU;              /* maximum packet data size */
                LONG  bps;              /* line rate (bits/sec) */
                LONG  HardwareType;     /* what the wire is */

            /*
            ** Format specific information
            */
            };

        The SizeAvailable specifies the number of bytes that the caller
        is prepared to accomodate, including the standard information fields.

        SizeSupplied is the number of bytes actually supplied,
        including the standard information fields, which will not exceed
        SizeAvailable.

        &lt;devices/sana2.h&gt; includes constants for these values.  If your
        hardware does not have a number assigned to it, you must contact
        the AmigaOS development team to get a hardware number.

   IO REQUEST
        ios2_Command    - S2_DEVICEQUERY.
        ios2_StatData   - Pointer to Sana2DeviceQuery structure to fill in.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.
</pre>

== S2_DISCONNECT ==

This command complements <tt>S2_CONNECT</tt> in that it tears down a connection. It uses the same <tt>Sana2Connection</tt> structure and hooks, but most of these members are ignored.

The command should work as follows:

# The client must set up the <tt>Sana2Connection</tt> data structure, initialize the <tt>s2c_Size</tt>, <tt>s2c_Options</tt>, <tt>s2c_ErrorHook</tt>, <tt>s2c_ConnectHook</tt>, <tt>s2c_DisconnectHook</tt>, <tt>s2c_Login</tt> and <tt>s2c_Password</tt> fields. The <tt>s2c_Options</tt>, <tt>s2c_ConnectHook</tt>, <tt>s2c_Login</tt> and <tt>s2c_Password</tt> fields will be ignored, but the client should play things safe.
# A pointer to the <tt>Sana2Connection</tt> data structure is placed in the <tt>ios2_Data</tt> member of an <tt>IOSana2Req</tt>, the command is set to <tt>S2_DISCONNECT</tt> and the request is sent via <tt>DoIO()</tt> or <tt>SendIO()</tt>.
# The driver receives the request and proceeds to reverse the steps that previously allowed it to establish a connection. This includes telling the peer to shut down the link, but it does not include cleaning up the link layer device access, i.e. no I/O requests used for accessing a modem may be shut down yet.
# The <tt>s2c_DisconnectHook</tt> may be invoked with the proper parameters. Some drivers, such as for asynchronous PPP, will need the hook to tell the modem to hang up the line. Some drivers, such as for PPPoE, may not need this hook and thus ignore it.
# The initialization is reversed completely, all resources allocated when the connection was previously opened are released. The <tt>IOSana2Req</tt> is returned with an error code of zero, indicating success. The SANA-II event <tt>S2EVENT_DISCONNECT</tt> must be sent, and, if necessary, <tt>S2EVENT_OFFLINE</tt>.

The connect and disconnect hook functions must not be called from interrupt code. For each hook only a <tt>Task</tt> calling context of unknown priority must be assumed. Also, stack space is provided only to call <tt>exec.library</tt> and <tt>utility.library</tt> functions. The callback shall not place excessive data on the stack. Stack space should be considered limited.

I propose a command with the following semantics:

<pre>
   NAME
        S2_DISCONNECT -- Go offline and close a link layer connection
            previously established with S2_CONNECT.

   FUNCTION
        This command complements the S2_CONNECT command in that it
        reverses the steps taken to establish a link layer connection.

   IO REQUEST
        ios2_Command          - S2_DISCONNECT
        ios2_Data             - Pointer to Sana2Connection structure
        ios2_BufferManagement - Magic cookie as returned when opening the
                                driver with a struct IOSana2Req

   RESULTS
        ios2_Req.io_Error - Zero if successful; non-zero otherwise
        ios2_WireError    - More specific error number

   NOTES
        If successful, this command implies S2_OFFLINE, i.e. the
        link layer is deallocated.

        The driver must ignore the S2_DISCONNECT command and
        recover gracefully if the S2_CONNECT was never sent or
        returned with an error.

        The contents of the Sana2Connection structure are valid only
        until the device driver has processed the command and returned
        the IOSana2Req. Any data the driver may need to retain beyond
        that point of time must be copied.

        Once the S2_DISCONNECT command has returned, it is safe to dispose
        of the Sana2Connection structure provided at S2_CONNECT time.

        Note that S2_OFFLINE does not necessarily imply S2_DISCONNECT, if
        the S2_DISCONNECT command is listed as supported by the driver via
        NSCMD_DEVICEQUERY. If S2_DISCONNECT is not listed as supported,
        S2_OFFLINE obviously implies disconnect functionality.

        S2_CONNECT/S2_DISCONNECT do not nest.

   SEE ALSO
        S2_CONNECT
</pre>

I propose that the following command number should be assigned:

<tt>#define S2_DISCONNECT 0xC006</tt>

== S2_GETDNSADDRESS ==

The PPP negotiation process may produce information on which domain name and NetBIOS name servers are available to the client. I think that it is doubtful that the availability of NetBIOS name servers will be useful for Amiga software (let alone whether NetBIOS name resolution has a future), which is why I suggest that a SANA-II command for returning only the domain name servers is introduced.

I propose a command with the following semantics:

<pre>
   NAME
        S2_GETDNSADDRESS -- Obtain the addresses of the primary
           and secondary domain name servers.

   FUNCTION
        Obtain the addresses of the domain name servers available to the
        client using this driver's address.

   IO REQUEST
        ios2_Command          - S2_GETDNSADDRESS
        ios2_Flags            - Supported flags are:
                                SANA2IOB_QUICK
        ios2_BufferManagement - Magic cookie as returned when opening the
                                driver with a struct IOSana2Req

   RESULTS
        ios2_Req.io_Error - Zero if successful; non-zero otherwise
        ios2_WireError    - More specific error number
        ios2_SrcAddr      - Address of primary domain name server
        ios2_DstAddr      - Address of secondary domain name server

   NOTES
        The size of the address returned by S2_GETPEERADDRESS must not be
        different from the size as returned by the S2_DEVICEQUERY command. For
        example, if a 32 bit IPv4 address was advertized, the driver must not
        return a 128 bit IPv6 address instead.

        If the driver is unable to return the primary domain name server
        address (ios2_SrcAddr) or the secondary domain name server address
        (ios2_DstAddr) it must fill the respective address fields with zeroes.
        It is legal for a driver to respond to the S2_GETDNSADDRESS command
        with two zero addresses (both ios2_SrcAddr and ios2_DstAddr filled
        with zeroes).
</pre>

I propose that the following command number should be assigned:
<tt>#define S2_GETDNSADDRESS 0xC003</tt>

This command may be useful beyond the typical application (PPP) described above.

== S2_GETEXTENDEDGLOBALSTATS ==

There already exists a SANA-II command for querying global device statistics (<tt>S2_GETGLOBALSTATS</tt>) which should be common to all kinds of networking devices. Statistics that are particular to a certain device type are intended to be returned through the <tt>S2_GETSPECIALSTATS</tt> command. I feel that these mechanisms both fail to cater well enough for dial-up or session-oriented networking applications such as PPP or PPPoE. Since the data structure used by <tt>S2_GETGLOBALSTATS</tt> is of a fixed size and not intended to accomodate for new fields, I propose to introduce a new command which uses a different data structure, as follows:

<syntaxhighlight lang="C" line>
struct Sana2ExtDeviceStats
{
   ULONG          s2xds_Length;
   ULONG          s2xds_Actual;

   S2QUAD         s2xds_PacketsReceived;
   S2QUAD         s2xds_PacketsSent;
   S2QUAD         s2xds_BadData;
   S2QUAD         s2xds_Overruns;
   S2QUAD         s2xds_UnknownTypesReceived;
   S2QUAD         s2xds_Reconfigurations;
   struct timeval s2xds_LastStart;

   struct timeval s2xds_LastConnected;
   struct timeval s2xds_LastDisconnected;
   struct timeval s2xds_TimeConnected;
};
</syntaxhighlight>

Before I proceed to explain what purposes the individual members serve, a few words on the <tt>S2QUAD</tt> type, which is defined as follows:
<tt>typedef struct { ULONG s2q_High; ULONG s2q_Low; } S2QUAD;</tt>

In other words, the <tt>S2QUAD</tt> type stands for an unsigned 64 bit big endian integer, as expressed in ISO 'C' terms.

The structure members have the following purposes:

<tt>s2xds_Length</tt>

This is the size of the data structure to be filled in and should be initialized by the caller to <tt>sizeof(struct Sana2ExtDeviceStats)</tt>. Smaller values are permitted, but these '''must not''' be smaller than 8 (which covers the <tt>s2xds_Length</tt> and <tt>s2xds_Actual</tt> members). A driver which finds an <tt>s2xds_Length</tt> &lt; 8 '''must''' treat this as an error and reject the command with <tt>ios2_Req.io_Error</tt>=<tt>IOERR_BADLENGTH</tt>.

<tt>s2xds_Actual</tt>

The size of the data structure filled with information. This member is initialized by the driver and '''must''' be &lt;= <tt>s2xds_Length</tt>.

<tt>s2xds_PacketsReceived</tt>

Number of packets that this unit has received. This is a 64 bit integer.

<tt>s2xds_PacketsSent</tt>

Number of packets that this unit has sent. This is a 64 bit integer.

<tt>s2xds_BadData</tt>

Number of bad packets received (i.e., hardware CRC failed). This is a 64 bit integer.

<tt>s2xds_Overruns</tt>

Number of packets dropped due to insufficient resources available in the network interface. This is a 64 bit integer.

<tt>s2xds_UnknownTypesReceived</tt>

Number of packets received that had no pending read command with the appropriate packet type. This is a 64 bit integer.

<tt>s2xds_Reconfigurations</tt>

Number of network reconfigurations since this unit was last configured. This is a 64 bit integer.

<tt>s2xds_LastStart</tt>

The time when this unit last went on-line.

<tt>s2xds_LastConnected</tt>

The time when this unit last established a connection. For dial-up connections, this should be the time when the underlying serial line started to accumulate costs. <tt>s2xds_LastConnected</tt> '''must''' be set to zero if the unit never managed to make a connection.

<tt>s2xds_LastDisconnected</tt>

The time when this unit last shut down a connection. For dial-up connections, this should be the time when the underlying serial line stopped accumulating costs, e.g. when the modem's carrier signal was lost. <tt>s2xds_LastDisconnected</tt> '''must''' be set to zero if the unit never disconnected.

<tt>s2xds_TimeConnected</tt>

The time this unit has been connected. For dial-up connections this should be the time between now and when the underlying serial line started accumulating costs.

If this unit is not currently connected, then <tt>s2xds_TimeConnected</tt> '''must''' be set to zero. This means in particular that when the connection is lost, <tt>s2xds_TimeConnected</tt> '''must''' be immediately set to zero and <tt>s2xds_LastDisconnected</tt> '''must''' be filled in so that client software can query how long the unit was connected by subtracting <tt>s2xds_LastConnected</tt> from <tt>s2xds_LastDisconnected</tt>.

If this unit is currently connected, <tt>s2xds_TimeConnected</tt> '''must never''' be zero; if necessary, set <tt>s2xds_TimeConnected.tv_secs=0</tt> and <tt>s2xds_TimeConnected.tv_micros=1</tt>.

If <tt>s2xds_TimeConnected</tt> is zero, check <tt>s2xds_LastConnected</tt> and <tt>s2xds_LastDisconnected</tt>; if the latter two are not zero, you can calculate the previous connection time by subtracting <tt>s2xds_LastConnected</tt> from <tt>s2xds_LastDisconnected</tt>.

The layout and semantics used by the <tt>Sana2ExtDeviceStats</tt> data structure suggest that there is a difference between the underlying networking media (the link layer) and the state of the protocol that is running on top of it. With drivers for networking hardware such as Ethernet there was no difference between these two, but for protocols like SLIP, PPP or PPPoE there is a difference. The difference is in that a session or connection may exist for a certain time whereas the protocol running inside that session may be switched 'online' later. The primary purpose of the <tt>s2xds_LastConnected</tt>, <tt>s2xds_LastDisconnected</tt> and <tt>s2xds_TimeConnected</tt> fields is to allow for cost accounting and traffic monitoring (so that, for example, a driver may be disconnected after it has been idle for a while) to be written.

I propose a command with the following semantics:

<pre>
   NAME
        S2_GETEXTENDEDGLOBALSTATS -- Get interface accumulated statistics;
           updated version.

   FUNCTION
        This command causes the device driver to retrieve various global
        runtime statistics for this network interface. The format of the data
        returned is as follows:

           struct Sana2ExtDeviceStats
           {
              ULONG s2xds_Length;
              ULONG s2xds_Actual;

              S2QUAD s2xds_PacketsReceived;
              S2QUAD s2xds_PacketsSent;
              S2QUAD s2xds_BadData;
              S2QUAD s2xds_Overruns;
              S2QUAD s2xds_UnknownTypesReceived;
              S2QUAD s2xds_Reconfigurations;
              struct timeval s2xds_LastStart;

              struct timeval s2xds_LastConnected;
              struct timeval s2xds_LastDisconnected;
              struct timeval s2xds_TimeConnected;
           };

   IO REQUEST
        ios2_Command          - S2_GETEXTENDEDGLOBALSTATS
        ios2_StatData         - Pointer to Sana2ExtDeviceStats structure
                                to fill in
        ios2_BufferManagement - Magic cookie as returned when opening the
                                driver with a struct IOSana2Req

   RESULTS
        ios2_Req.io_Error - Zero if successful; non-zero otherwise
        ios2_WireError    - More specific error number
</pre>

I propose that the following command number should be assigned:
<tt>#define S2_GETEXTENDEDGLOBALSTATS 0xC004</tt>

== S2_GETGLOBALSTATS ==

<pre>
   NAME
        GetGlobalStats -- Get interface accumulated statistics.

   FUNCTION
        This command causes the device driver to retrieve various global
        runtime statistics for this network interface. The format of the
        data returned is as follows:

            struct Sana2DeviceStats
            {
                ULONG PacketsReceived;
                ULONG PacketsSent;
                ULONG BadData;
                ULONG Overruns;
                ULONG UnknownTypesReceived;
                ULONG Reconfigurations;
                timeval LastStart;
            };

   IO REQUEST
        ios2_Command    - S2_GETGLOBALSTATS.
        ios2_StatData   - Pointer to Sana2DeviceStats structure to fill.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.

   SEE ALSO
        S2_GETSPECIALSTATS
</pre>

== S2_GETNETWORKINFO ==

<pre>
   NAME
	S2_GETNETWORKINFO -- Get information on current network.

   FUNCTION
	This command provides information on the status of the currently
	used network. If this command completes successfully, ios2_StatData
	will contain a pointer to a tag list that contains information on
	the network. The S2INFO_#? tags used are defined in the
	devices/sana2wireless.h include file.

	The returned taglist is allocated from the supplied memory pool.
	To discard the results of this command, the entire memory pool
	should be destroyed.

   INPUTS
	ios2_Data - Pointer to an Exec memory pool.

   RESULTS
	ios2_Data - Remains unchanged.
	ios2_StatData - Pointer to a tag list.
	io_Error - Zero if successful; non-zero otherwise.
	ios2_WireError - More specific error code.
</pre>

== S2_GETNETWORKS ==

<pre>
   NAME
	S2_GETNETWORKS -- Scan for available networks.

   FUNCTION
	This command supplies details of available networks. If the scan
	should be limited to one specific network, the S2INFO_SSID tag
	should specify its name.

	If this command completes successfully, ios2_StatData will contain
	an array of pointers to tag lists, each of which contains
	information on a single network. The device will set ios2_DataLength
	to the number of elements in this array.

	The returned taglists are allocated from the supplied memory pool.
	To discard the results of this command, the entire memory pool
	should be destroyed.

	The S2INFO_#? tags used with this command are defined in the
	devices/sana2wireless.h include file.

   INPUTS
	ios2_Data - Pointer to an Exec memory pool.
	ios2_StatData - Pointer to taglist that specifies parameters to use.

   RESULTS
	ios2_DataLength - Number of tag lists returned.
	ios2_Data - Remains unchanged.
	ios2_StatData - Pointer to an array of tag lists.
	io_Error - Zero if successful; non-zero otherwise.
	ios2_WireError - More specific error code.
</pre>

== S2_GETPEERADDRESS ==

As part of the negotiation process, the PPP protocol can return the addresses used by the peer (the other side of the point-to-point connection PPP establishes; typically a dial-in server) and assigned to the client establishing the connection. The SANA-II standard does not provide for a mechanism to return such information. Existing drivers therefore had to resort to other means, such as by setting global environment variables containing this information.

I propose a command with the following semantics:

<pre>
   NAME
        S2_GETPEERADDRESS -- Obtain the addresses used by the peer
           (server) and the client of a point-to-point connection.

   FUNCTION
        Obtain the address used by the peer of a point-to-point connection and
        the address assigned to the local driver.

   IO REQUEST
        ios2_Command          - S2_GETPEERADDRESS
        ios2_Flags            - Supported flags are:
                                SANA2IOB_QUICK
        ios2_BufferManagement - Magic cookie as returned when opening the
                                driver with a struct IOSana2Req

   RESULTS
        ios2_Req.io_Error - Zero if successful; non-zero otherwise
        ios2_WireError    - More specific error number
        ios2_SrcAddr      - Address assigned to the local driver
        ios2_DstAddr      - Address used by the peer

   NOTES
        The size of the address returned by S2_GETPEERADDRESS must not be
        different from the size returned by the S2_DEVICEQUERY command. For
        example, if a 32 bit IPv4 address was advertized, the driver must not
        return a 128 bit IPv6 address instead.

        If the driver is unable to return the local driver address
        (ios2_SrcAddr) or the peer's address (ios2_DstAddr) it must fill the
        respective address fields with zeroes. It is legal for a driver to
        respond to the S2_GETPEERADDRESS command with two zero addresses (both
        ios2_SrcAddr and ios2_DstAddr filled with zeroes).
</pre>

I propose that the following command number should be assigned:
<tt>#define S2_GETPEERADDRESS 0xC002</tt>

This command may be useful beyond the typical application (PPP) described above.

== S2_GETSIGNALQUALITY ==

<pre>
   NAME
	S2_GETSIGNALQUALITY -- Get signal quality statistics.

   FUNCTION
	This command fills in the supplied Sana2SignalQuality structure with
	current signal and noise levels. The unit for these figures is dBm.
	Typically, they are negative values.

   INPUTS
	ios2_StatData - Pointer to Sana2SignalQuality structure.

   RESULTS
	io_Error - Zero if successful; non-zero otherwise.
	ios2_WireError - More specific error code.
	ios2_StatData - Pointer to filled Sana2SignalQuality structure.
</pre>

== S2_GETSPECIALSTATS ==

<pre>
   NAME
        S2_GETSPECIALSTATS -- Get network type specific statistics.

   FUNCTION
        This function returns statistics which are specific to the type of
        network medium this driver controls. For example, this command could
        return statistics common to all Ethernets which are not common to all
        network mediums in general.

        The supplied Sana2SpecialStatData structure is given below:

            struct Sana2SpecialStatData
            {
                ULONG RecordCountMax;
                ULONG RecordCountSupplied;
                struct Sana2StatRecord[RecordCountMax];
            };

        The format of the data returned is:

            struct Sana2StatRecord
            {
                ULONG Type;     /* Amiga registered */
                LONG Count;     /* the stat itself */
                char *String;   /* null terminated */
            };

        The RecordCountMax field specifies the number of records that the
        caller is prepared to accomodate.

        RecordCountSupplied is the number of record actually supplied which
        will not exceed RecordCountMax.

   IO REQUEST
        ios2_Command    - S2_GETSPECIALSTATS.
        ios2_StatData   - Pointer to a Sana2SpecialStatData structure to fill.
                          RecordCountMax must be initialized.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.

   NOTES
        Amiga will maintain registered statistic Types.

   SEE ALSO
        S2_GETGLOBALSTATS, &lt;devices/sana2specialstats.h&gt;
</pre>

== S2_GETSTATIONADDRESS ==

<pre>
   NAME
        S2_GETSTATIONADDRESS -- Get default and interface address.

   FUNCTION
        This command causes the device driver to copy the current interface
        address into ios2_SrcAddr, and to copy the factory default station
        address (if any) into ios2_DstAddr.

   IO REQUEST
        ios2_Command    - S2_GETSTATIONADDRESS.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.
        ios2_SrcAddr    - Current interface address.
        ios2_DstAddr    - Default interface address (if any).

   SEE ALSO
        S2_CONFIGINTERFACE
</pre>

== S2_GETTYPESTATS ==

<pre>
   NAME
        S2_GETTYPESTATS -- Get accumulated type specific statistics.

   FUNCTION
        This command causes the device driver to retrieve various packet type
        specific runtime statistics for this network interface. The format of
        the data returned is as follows:

            struct Sana2TypeStatData
            {
                LONG PacketsSent;
                LONG PacketsReceived;
                LONG BytesSent;
                LONG BytesReceived;
                LONG PacketsDropped;
            };

   IO REQUEST
        ios2_Command    - S2_GETTYPESTATS.
        ios2_PacketType - Packet type of interest.
        ios2_StatData   - Pointer to TypeStatData structure to fill in.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.

   NOTES
        Statistics for a particular packet type are only available while that
        packet type is being ``tracked''.

   SEE ALSO
        S2_TRACKTYPE, S2_UNTRACKTYPE
</pre>

== S2_MULTICAST ==

<pre>
   NAME
        S2_MULTICAST -- Multicast a packet on network.

   FUNCTION
        This command works the same as CMD_WRITE except that it also performs
        whatever special processing of the packet is required to do a
        multicast send. The actual multicast mechanism is neccessarily
        network/interface/device specific.

   IO REQUEST
        ios2_Command    - S2_MULTICAST.
        ios2_Flags      - Supported flags are:
                                SANA2IOB_RAW
                                SANA2IOB_QUICK
        ios2_PacketType - Packet type to send.
        ios2_DstAddr    - Destination interface address for this packet.
        ios2_DataLength - Length of the Data to be sent.
        ios2_Data       - Abstract data structure which packet data is
                          contained in.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
                          This command can fail for many reasons and
                          is not supported by all networks and/or
                          network interfaces.
        ios2_WireError  - More specific error number.

   NOTES
        The address supplied in ios2_DstAddr will be sanity checked (if
        possible) by the driver. If the supplied address fails this sanity
        check, the multicast request will fail immediately with ios2_Error
        set to S2WERR_BAD_MULTICAST.

        Another Amiga will not receive a multicast packet unless it has had
        the particular multicast address being used S2_ADDMULTICASTADRESS'd.

   SEE ALSO
        CMD_WRITE, S2_BROADCAST, S2_ADDMULTICASTADDRESS
</pre>

== S2_OFFLINE ==

<pre>
   NAME
        S2_OFFLINE -- Remove interface from service.

   FUNCTION
        This command removes a network interface from service.

   IO REQUEST
        ios2_Command    - S2_OFFLINE.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.

   NOTES
        Aborts all pending reads and writes with ios2_Error set to
        S2ERR_OUTOFSERVICE.

        While the interface is offline, all read, writes and any other
        command that touches interface hardware will be rejected with
        ios2_Error set to S2ERR_OUTOFSERVICE.

        This command is intended to permit a network interface to be
        tested on an otherwise live system.

   SEE ALSO
        S2_ONLINE
</pre>

== S2_ONEVENT ==

<pre>
   NAME
        S2_ONEVENT -- Return when specified event occures.

   FUNCTION
        This command returns when a particular event condition has occured
        on the network or this network interface.

   IO REQUEST
        ios2_Command    - S2_ONEVENT.
        ios2_Flags      - Supported flags are:
                                SANA2IOB_QUICK
        ios2_WireError  - Mask of event(s) to wait for
                          (from &lt;devices/sana2.h&gt;).

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - Mask of events that occured.

   NOTES
        If this device driver does not understand the specified event
        condition(s) then the command returns immediately with
        ios2_Req.io_Error set to S2_ERR_NOT_SUPPORTED and ios2_WireError
        S2WERR_BAD_EVENT.  A successful return will have ios2_Error set to
        zero ios2_WireError set to the event number.

        All pending requests for a particular event will be returned when
        that event occurs.

        All event types that cover a particular condition are returned when
        that condition occures. For instance, if an error is returned by
        a buffer management function during receive processing, events of
        types S2EVENT_ERROR, S2EVENT_RX and S2EVENT_BUFF would be returned if
        pending.

        Types ONLINE and OFFLINE return immediately if the device is
        already in the state to be waited for.
</pre>

== S2_ONLINE ==

<pre>
   NAME
        S2_ONLINE -- Put a network interface back in service.

   FUNCTION
        This command places an offline network interface back into service.

   IO REQUEST
        ios2_Command    - S2_ONLINE.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.

   NOTES
        This command is responsible for putting the network interface
        hardware back into a known state (as close as possible to the
        state before S2_OFFLINE) and resets the unit global and special
        statistics.

   SEE ALSO
        S2_OFFLINE
</pre>

== S2_READMGMT ==

<pre>
   NAME
	S2_READMGMT -- Read a management frame.

   FUNCTION
	Reads a raw IEEE 802.11 management frame from the device. The buffer
	management mechanism used is simpler than that used for data
	packets (e.g. with CMD_READ): a buffer pointer and a length value
	are passed to the device.

	As with CMDREAD/S2_READORPHAN, multiple S2_READMGMT requests can
	(and should) be queued concurrently to ensure no incoming frames are
	missed.

   INPUTS
	ios2_DataLength - size of frame buffer.
	ios2_Data - pointer to a frame buffer.

   RESULTS
	io_Error - Zero if successful; non-zero otherwise.
	ios2_WireError - More specific error code.
	ios2_DataLength - actual size of received frame.
	ios2_Data - pointer to the filled frame buffer.
</pre>

== S2_READORPHAN ==

<pre>
   NAME
        S2_READORPHAN -- Get a packet for which there is no reader.

   FUNCTION
        Get the next packet available that does not satisfy any then-pending
        CMD_READ requests. The data returned in the ios2_Data structure is
        normally the Data Link Layer packet type field and the packet data. If
        bit SANA2IOB_RAW is set in ios2_Flags, then the entire Data Link Layer
        packet, including both header and trailer information, will be
        returned.

   IO REQUEST
        ios2_Command    - CMD_READORPHAN.
        ios2_Flags      - Supported flags are:
                                SANA2IOB_RAW
                                SANA2IOB_QUICK
        ios2_DataLength - Length of the Data to be sent.
        ios2_Data       - Abstract data structure which packet data is
                          contained in.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.
        ios2_Flags      - The following flags may be returned:
                                SANA2IOB_RAW
                                SANA2IOB_BCAST
                                SANA2IOB_MCAST
        ios2_SrcAddr    - Source interface address of packet.
        ios2_DstAddr    - Destination interface address of packet.
        ios2_DataLength - Length of the Data to be sent.
        ios2_Data       - Abstract data structure which packet data is
                          contained in.

   NOTES
        This is intended for debugging and management tools.  Protocols should
        not use this.

        As with 802.3 packets on an ethernet, to determine which protocol
        family the returned packet belongs to you may have to specify
        SANA2IOB_RAW to get the entire data link layer wrapper (which is where
        the protocol type may be kept). Notice this necessarily means that
        this cannot be done in a network interface independent fashion.  The
        driver will, however, fill in the PacketType field to the best of its
        ability.

   SEE ALSO
        CMD_READ, CMD_WRITE
</pre>

== S2_SAMPLE_THROUGHPUT ==

The SANA-II standard already allows for statistics to be returned on the amount of data that has passed through a driver. Unfortunately, that information is not very accurate in that no information is provided on the time span in which the data was accumulated. Such information would be helpful in trying to determine as accurately as possible how large the data throughput actually is. I therefore propose a new command which can be used to obtain that information, which uses the following data structure:

<syntaxhighlight lang="C" line>
struct Sana2ThroughputStats
{
   ULONG          s2ts_Length;
   ULONG          s2ts_Actual;

   struct Task *  s2ts_NotifyTask;
   ULONG          s2ts_NotifyMask;

   struct timeval s2ts_StartTime;
   struct timeval s2ts_EndTime;
   S2QUAD         s2ts_BytesSent;
   S2QUAD         s2ts_BytesReceived;
   S2QUAD         s2ts_Updates;
};
</syntaxhighlight>

Before I proceed to explain what purposes the individual members serve, a few words on the <tt>S2QUAD</tt> type, which is defined as follows:

<tt>typedef struct { ULONG s2q_High; ULONG s2q_Low; } S2QUAD;</tt>

In other words, the <tt>S2QUAD</tt> type stands for an unsigned 64 bit big endian integer, as expressed in ISO 'C' terms.

The structure members have the following purposes:

<tt>s2ts_Length</tt>

This is the size of the data structure to be filled in and should be initialized by the caller to <tt>sizeof(struct Sana2ThroughputStats)</tt>. Smaller values are permitted, but these must not be smaller than 8 (which covers the <tt>s2ts_Length</tt> and <tt>s2ts_Actual</tt> members). A driver which finds an <tt>s2ts_Length</tt> &lt; 8 must treat this as an error and reject the command with <tt>ios2_Req.io_Error</tt>=<tt>IOERR_BADLENGTH</tt>.

<tt>s2ts_Actual</tt>

The size of the data structure filled with information. This member is initialized by the driver and must be &lt;= <tt>s2ts_Length</tt>.

<tt>s2ts_NotifyTask</tt>

The <tt>Task</tt> to notify whenever the contents of this data structure change. This must be <tt>NULL</tt> if no notification is desired.

'''Note:''' This feature should be used carefully, as so much data may arrive that the driver will almost be constantly signalling this <tt>Task</tt> that a change has taken place.

It is recommend that periodic polling be used, such as to update displays of a link monitoring program.

<tt>s2ts_NotifyMask</tt>

The signal mask to use for notifying the <tt>Task</tt> whose address is found in <tt>s2ts_NotifyTask</tt> (via <tt>Signal(s2ts-&gt;s2ts_NotifyTask,s2ts-&gt;s2ts_NotifyMask);</tt>). This must be zero if no notification is desired.

<tt>s2ts_StartTime</tt>

The time when the driver started to fill in this data structure.

<tt>s2ts_EndTime</tt>

The time when the driver last updated the contents of this data structure.

<tt>s2ts_BytesSent</tt>

Total number of bytes sent since the driver started to fill in this data structure. This is a 64 bit integer.

<tt>s2ts_BytesReceived</tt>

Total number of bytes received since the driver started to fill in this data structure. This is a 64 bit integer.

<tt>s2ts_Updates</tt>

Number of times the driver has updated this data structure. This value will increase with every change. This is a 64 bit integer.

A driver implementing this command should take care to update the members <tt>s2ts_EndTime</tt>, <tt>s2ts_BytesSent</tt>, <tt>s2ts_BytesReceived</tt> and <tt>s2ts_Updates</tt> atomically each time changes are made.

I propose a command with the following semantics:

<pre>
   NAME
        S2_SAMPLE_THROUGHPUT -- Obtain accurate information on
           driver data throughput.

   FUNCTION
        This command installs a data structure which is updated every time
        data is sent or received by the driver.

        This command must be sent via SendIO() or BeginIO(); until
        the associated I/O request is recalled using AbortIO(), the
        device unit will continue to update the Sana2ThroughputStats
        structure in real time.

   IO REQUEST
        ios2_Command          - S2_SAMPLE_THROUGHPUT
        ios2_StatData         - Pointer to Sana2ThroughputStats structure
                                to fill in
        ios2_BufferManagement - Magic cookie as returned when opening the
                                driver with a struct IOSana2Req

   RESULTS
        ios2_Req.io_Error - Zero if successful; non-zero otherwise

   NOTES
        If this device driver does not understand this command,
        it will immediately return the IOSana2Req with
        ios2_Req.io_Error set to IOERR_NOCMD. Otherwise, the request will
        remain queued until it is removed with AbortIO() later.
</pre>

I propose that the following command number should be assigned:

<tt>#define S2_SAMPLE_THROUGHPUT 0xC007</tt>

== S2_SANA2HOOK ==

The 'traditional' copy call-back functions are installed at <tt>OpenDevice()</tt> time, and they are found in a <tt>TagItem</tt> list which is passed along with the <tt>IOSana2Req</tt>. For the new <tt>Hook</tt>-based call-back functionality, I propose to introduce a new command. This command would take care of installing one single <tt>Hook</tt> which will be invoked with the parameters described in section 3.1. The hook function can key off the <tt>SANA2HookMsg-&gt;schm_Method</tt> field to figure out which function should be performed. Once the <tt>Hook</tt> is installed via the command as follows, the driver shall ignore any and all tags passed to it during <tt>OpenDevice()</tt> time.

<pre>
   NAME
        S2_SANA2HOOK -- Install a Hook to perform operations such as copying,
                        overriding the call-back functions installed at
                        OpenDevice() time.

   FUNCTION
        The S2_SANA2HOOK command is to replace the 'traditional' call-back
        functions installed through the TagItem list found in the
        IOSana2Req-&gt;ios2_BufferManagement field. Instead of assigning a
        function pointer for each copying function, all operations are
        to be performed through a Hook. This is intended to make the
        interface more portable and less dependant on a certain hardware
        architecture.

        The hook message and the hook data structures allow for more than
        copying to be done.

   IO REQUEST
        ios2_Command    - S2_SANA2HOOK
        ios2_Data       - Points to a struct Sana2Hook, which looks
                          like this:

                             struct Sana2Hook
                             {
                                struct Hook s2h_Hook;
                                Tag *       s2h_Methods;
                             };

                          The structure fields have the following purposes:

                             s2h_Hook
                                A standard Hook structure, ready to be
                                called. Once installed, the complete Hook
                                structure including its Node structure is
                                off limits! The s2h_Hook remains installed
                                until CloseDevice().

                             s2h_Methods
                                Points to a table of Tag values, each
                                identifying a copy method supported
                                (S2_CopyToBuff, S2_CopyFromBuff,
                                S2_CopyToBuff16, S2_CopyFromBuff16,
                                S2_CopyToBuff32, S2_CopyFromBuff32,
                                S2_DMACopyToBuff32, S2_DMACopyFromBuff32,
                                S2_DMACopyToBuff64 or S2_DMACopyFromBuff64)
                                or the logging facility (S2_Log).
                                The table must be terminated by TAG_END.

                                The driver will check the table and
                                verify that the mandatory S2_CopyToBuff
                                and S2_CopyFromBuff commands are present.
                                Additional functionality is used as
                                available if the driver supports it.

        ios2_DataLength - Must be &gt;= 20, which is the default length of
                          the Sana2Hook structure. This may grow in
                          the future, and larger values for ios2_DataLength
                          may indicate additional functionality associated
                          with the Sana2Hook.

   RESULTS
        ios2_Error      - IOERR_NOCMD if this command is not supported
                          by the driver.

                          IOERR_BADLENGTH if IOSana2Req-&gt;ios2_DataLength
                          is &lt; 20.

                          IOERR_UNITBUSY if the Hook was already
                          installed or if any of the CMD_READ, CMD_WRITE,
                          S2_MULTICAST or S2_BROADCAST have already been
                          invoked.

                          S2WERR_FUNCTIONS_MISSING if the table pointed
                          to by Sana2Hook-&gt;s2h_Methods does not
                          include the S2_CopyToBuff and S2_CopyFromBuff
                          tags.

   NOTES
        The S2_SANA2HOOK command shall be invoked right after OpenDevice()
        as very first command.

        When the command has been executed, the driver must use the
        newly installed Hook for all its copying or logging and cease to
        use the call-back functions provided at OpenDevice() time.

        The contents of the Sana2Hook structure, as passed to the
        driver, must not be modified. This includes the MinNode
        at the beginning of the Hook structure which the driver may
        need to use for its own purposes.

        The table pointed to by Sana2Hook-&gt;s2h_Methods must
        include at least the S2_CopyToBuff and S2_CopyFromBuff tags.
        It must be valid until CloseDevice() is called.

        This field is to be treated as private by a protocol stack.
        IOSana2Req structures may be duplicated by copying
        ios2_BufferManagement, io_Device, and io_Unit.
</pre>

The new command value is defined as follows:

<pre>
   #define S2_SANA2HOOK 0xC008
</pre>

The new error code is defined as follows:

<pre>
   #define S2WERR_FUNCTIONS_MISSING 24 /* mandatory copy functions are missing */
</pre>

== S2_SETKEY ==

<pre>
   NAME
	S2_SETKEY -- Set an encryption key.

   FUNCTION
	Sets one of the encryption keys for the device. Note that
	ios2_StatData, if used, points to a byte array representing the RX
	RX counter value.

   INPUTS
	ios2_WireError - Key index.
	ios2_PacketType - Encryption type (e.g. S2ENC_WEP).
	ios2_DataLength - Key length.
	ios2_Data - Key.
	ios2_StatData - RX counter number (NULL if unused).

   RESULTS
	io_Error - Zero if successful; non-zero otherwise.
	ios2_WireError - More specific error code.
</pre>

== S2_SETOPTIONS ==

<pre>
   NAME
	S2_SETOPTIONS -- Set network options.

   FUNCTION
	Set various parameters for the network interface. This command
	should be called before going online to set any essential parameters
	not covered elsewhere. The S2INFO_#? tags used are defined in the
	devices/sana2wireless.h include file.

   INPUTS
	ios2_Data - Pointer to a taglist that specifies parameters to use.

   RESULTS
	io_Error - Zero if successful; non-zero otherwise.
	ios2_WireError - More specific error code.
</pre>

== S2_TRACKTYPE ==

<pre>
   NAME
        S2_TRACKTYPE -- Accumulate statistics about a packet type.

   FUNCTION
        This command causes the device driver to accumulate statistics about
        a particular packet type. Packet type statistics, for the particular
        packet type, are zeroed by this command.

   IO REQUEST
        ios2_Command    - S2_TRACKTYPE.
        ios2_PacketType - Packet type of interest.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.

   SEE ALSO
        S2_UNTRACKTYPE, S2_GETTYPESTATS
</pre>

== S2_UNTRACKTYPE ==

<pre>
   NAME
        S2_UNTRACKTYPE -- End statistics about a packet type.

   FUNCTION
        This command causes the device driver to stop accumulating
        statistics about a particular packet type.

   IO REQUEST
        ios2_Command    - S2_UNTRACKTYPE.
        ios2_PacketType - Packet type of interest.

   RESULTS
        ios2_Error      - Zero if successful; non-zero otherwise.
        ios2_WireError  - More specific error number.

   SEE ALSO
        S2_TRACKTYPE, S2_GETTYPESTATS
</pre>

== S2_WRITEMGMT ==

<pre>
   NAME
	S2_WRITEMGMT -- Write a management frame.

   FUNCTION
	Writes a raw IEEE 802.11 management frame to the device. The buffer
	management mechanism used is simpler than that used for data
	packets (e.g. with CMD_WRITE): a buffer pointer and a length value
	are passed to the device.

   INPUTS
	ios2_DataLength - full frame length.
	ios2_Data - pointer to a complete IEEE 802.11 management frame.

   RESULTS
	io_Error - Zero if successful; non-zero otherwise.
	ios2_WireError - More specific error code.
</pre>

= Callback mechanisms =

== CopyFromBuff ==

<pre>
   NAME
        CopyFromBuff -- Copy n bytes from an abstract data structure.

   SYNOPSIS
        success = CopyFromBuff(to, from, n)
        d0                     a0  a1    d0

        BOOL CopyToBuff(VOID *, VOID *, ULONG);

   FUNCTION
        This function copies 'n' bytes of data in the abstract data structure
        pointed to by 'from' into the contigous memory pointed to by 'to'.
        'to' must contain at least 'n' bytes of usable memory or innocent
        memory will be overwritten.

   INPUTS
        to              - pointer to contiguous memory to copy to.
        from            - pointer to abstract structure to copy from.
        n               - number of bytes to copy.

   RESULT
        success         - TRUE if operation was successful, else FALSE.

   NOTES
        This function must be callable from interrupts.  In particular, this
        means that this function may not directly or indirectly call any
        system memory functions (since those functions rely on Forbid() to
        protect themselves) and that  you must not compile this function
        with stack checking enabled.  See the Exec Interrupts
        chapter for more details on what is legal in a routine called from
        an interrupt handler.

        'C' programmers should not compile with stack checking (option '-v'
        in SAS) and should geta4() or __saveds.</pre>

== CopyToBuff ==

<pre>
   NAME
        CopyToBuff -- Copy n bytes to an abstract data structure.

   SYNOPSIS
        success = CopyToBuff(to, from, n)
        d0                   a0  a1    d0

        BOOL CopyToBuff(VOID *, VOID *, ULONG);

   FUNCTION
        This function first does any initialization and/or allocation
        required to prepare the abstract data structure pointed at by 'to'
        to be filled with 'n' bytes of data from 'from'.  It then executes
        the copy operation.

        If, for example, there is not enough memory available to prepare
        the abstract data structure, the call is failed and FALSE is returned.

        The buffer management scheme should be such that any memory needed
        to fulfill CopyToBuff() calls is already allocated from the system
        before the call to CopyToBuff() is made.

   INPUTS
        to              - pointer to abstract structure to copy to.
        from            - pointer to contiguous memory to copy from.
        n               - number of bytes to copy.

   RESULT
        success         - TRUE if operation was successful, else FALSE.

   NOTES
        This function must be callable from interrupts.  In particular, this
        means that this function may not directly or indirectly call any
        system memory functions (since those functions rely on Forbid() to
        protect themselves) and that you must not compile this function
        with stack checking enabled.  See the Exec Interrupts
        chapter for more details on what is legal in a routine called from
        an interrupt handler.

        'C' programmers should not compile with stack checking (option '-v'
        in SAS) and should geta4() or __saveds.</pre>

== PacketFilter ==

<pre>
   NAME
        PacketFilter -- Perform filtering operation on CMD_READ's.

   SYNOPSIS
        keep = PacketFilter(hook, ios2, data)
        d0                   a0    a2    a1

        BOOL PacketFilter(struct Hook *, struct IOSana2Req *, APTR);

   FUNCTION
        This function (if supplied by a protocol stack) may be used to
        reject packets before they are copied into a protocol stack's
        internal buffers.

        The IOSana2Req structure should be set up to look (almost) exactly
        as it would if it was successfully returned for the current packet.
        Specifically, the fields that should be set up correctly are:

        ios2-&gt;ios2_DataLength
        ios2-&gt;ios2_SrcAddr
        ios2-&gt;ios2_DstAddr

       The &quot;data&quot; pointer must point to the beginning of the packet data
        that is stored in contiguous memory.  The data should NOT include
        any hardware specific headers (unless of course the CMD_READ
        request wanted RAW packets).

   INPUTS
        hook            - pointer to the Hook originally supplied during
                          OpenDevice().
        ios2            - The IOSana2Req CMD_READ request that will be used
                          (the &quot;object&quot; of the Hook call).
        data            - The packet data (the &quot;message&quot; of the Hook call).

   RESULT
        success         - TRUE if the driver should provide the packet to
                          the protocol stack, FALSE if the packet should be
                          ignored.

   NOTES
        This function must be callable from interrupts.  In particular, this  
        means that this function may not directly or indirectly call any
        system memory functions (since those functions rely on Forbid() to
        protect themselves) and that  you must not compile this function
        with stack checking enabled.  See the Exec Interrupts
        chapter for more details on what is legal in a routine called from
        an interrupt handler.

        'C' programmers should not compile with stack checking (option '-v'
        in SAS) and should geta4() or __saveds.

        What does packet filtering do? With the original ``SANA-II Network
        Device Driver Specification'', a protocol stack could open a device
        and ask for certain packet types. It got all the packets that matched
        this type. As it turned out, this could be mighty inefficient if there
        were packets that the protocol stack did not use at all. These would
        go into read processing of the protocol stack and waste CPU time even
        though they could have been easily identified on arrival.</pre>

= Ethernet description =

<pre>#define S2WireType_Ethernet             1

ios2_DataLength:
        valid ethernet packets have 64 to 1500 bytes of data.

Address format:
        Ethernet addresses consist of 47 bits of address information and
        a 1 bit multicast flag. The standard for expressing ethernet
        addresses is as 6 bytes (octets) in the order in which the bytes
        are transmitted with the low-order bits in a byte transmitted
        first. The multicast flag bit is the least-significant bit of the
        first byte.

        Ethernet addresses in a Sana2IOReq occupy the first 6 bytes of
        an address field in transmission order with the low-order bits in
        a byte transmitted first.

Station Address:
        Each ethernet board must have a unique ethernet hardware address.
        Drivers will override any attempt to set the address to anything
        other than the ROM address.

Raw reads and writes:
        6 bytes of destination address,
        6 bytes of source address,
        2 bytes of type,
        64 to 1500 bytes of data
        (followed by 4 byte CRC value covering all of the above
         which is hardware generated and checked, hence not included
         in even raw packets)

Multicast:      Supported

Broadcast:      Supported

Promiscuous:    Supported

Packet Type Numbers for Ethernet are assigned by:

        Xerox Corporation
        Xerox Systems Institute
        475 Oakmead Parkway, Sunnyvale, CA 94086
        Attn: Ms. Fonda Pallone
        (408) 737-4652


Some Common Packet Type Numbers:

      decimal  Hex        Description
      -------  ---        -----------
         000   0000-05DC  IEEE 802.3 Length Field
        2048   0800       TCP/IP -- IP
        2054   0806       TCP/IP -- ARP
       32821   8035       TCP/IP -- RARP
       32923   809B       Appletalk
       33011   80F3       AppleTalk AARP (Kinetics)
       33100   814C       SNMP
       33079   8137-8138  Novell, Inc.</pre>
= ARCNET description =

<pre>S2WireType_Arcnet       7

ios2_DataLength:
        506 byte MTU (because of the possibility of two byte Types).
        Packets of size 254, 255, or 256 bytes are padded to 257 bytes before
        transmition.

Station Address:
        ARCNET hardware may have addresses set with jumpers, DIP switches
        or software.  Different drivers may therefore behave differently
        with S2_CONFIGINTERFACE.

        Hardware addresses should be assigned by users from highest to lowest
        because there is some efficiency gained in the token passing scheme
        this way.  For example, on a three node network, hardware numbers 254,
        253 and 252 should be used rather than 1, 2 and 3.

Raw reads and writes:
        Short Packets (1-253 bytes)
                Destination Address             (1 byte)
                Source Address                  (1 byte)
                Count (256-N-Type length)       (1 byte)
                Padding                         (to byte number Count)
                Type                            (1 or 2 bytes)
                Data                            (N bytes)

        Long Packets (257-506 bytes)
                Destination Address             (1 byte)
                Source Address                  (1 byte)
                zero                            (1 byte)
                Count (512-N-Type length)       (1 byte)
                Padding                         (to byte number Count)
                Type                            (1 or 2 bytes)
                Data                            (N bytes)


Multicast:      Not Supported

Broadcast:      Supported

Promiscuous:    Generally Not Supported

Packet Type Numbers for ARCNET are assigned by:
        Datapoint Corporation

Some Common Packet Type Numbers

        decimal  hex    description
        -------  ---    -----------
        221      DD     AppleTalk
        240      F0     TCP/IP -- IP   (RFC 1051)
        241      F1     TCP/IP -- ARP  (RFC 1051)
        212      F0     TCP/IP    IP   (RFC 1201, proposed)
        213      F1     TCP/IP -- ARP  (RFC 1201, proposed)
        214      D6     TCP/IP -- RARP (RFC 1201, proposed)
        247      F7     Banyan Vines
        250      FA     Novell IPX
</pre>

= Authors =

== Revision 2 & 3 ==

Copyright © 1992-2000 Amiga, Inc. All Rights Reserved<br />

Amiga is a registered trademark of Amiga, Inc. Ethernet is a trademark of Xerox Corporation. ARCNET is a trademark of Datapoint Corporation. DECNet is a trademark of Digital Equipment Corporation. AppleTalk is a trademark of Apple Computer, Inc.

== Revision 4 ==

Extending the SANA-II network driver specification<br />
by Olaf Barthel

(Last updated 02-Mar-2003)

== Revision 5 ==

Hook-based callback function extensions for SANA-II (SANA-IIR5) <br />
by Olaf Barthel and Heinz Wrobel

== Revision 6 ==

SANA-II IEEE 802.11 wireless API <br/>
by Neil Cafferkey

= Acknowledgments =

== Revision 2 & 3 ==

Many people and companies have contributed to the "SANA-II Network Device Driver Specification". The original SANA-II Autodocs and includes were put together by Ray Brand, Perry Kivolowitz (ASDG) and Martin Hunt. Those original documents evolved to their current state and grew to include this document at the hands of Dale Larson and Greg Miller. Brian Jackson and John Orr provided valuable editing. Randell Jesup has provided sage advice on several occasions. The buffer management callback mechanism was his idea. Dale Luck (GfxBase) and Rick Spanbauer (Ameristar Technologies) have provided valuable comments throughout the process. Nicolas Benezan (ADONIS) provided many detailed and useful comments on weaknesses in late drafts of the specification. The enhancements for better buffer management, clarifications and notes for device implementers were added to the specification by Heinz Wrobel whilst consulting for Amiga Technologies GmbH, yielding revision 3.0 of the specification.

Thanks to all the above and the numerous others who have contributed with their comments, questions and discussions.

= Changes =

== Revision 4 ==

Changes since 24-Dec-2002:

* The <tt>Sana2Connection</tt> data structure used by the <tt>S2_CONNECT</tt> command now must remain valid until the <tt>S2_DISCONNECT</tt> command is sent (see [[#2.4|section 2.4]]).

Changes since 01-May-2002:

* Added the <tt>ppp.async.readrequests</tt> and <tt>ppp.async.eof</tt> configuration keywords.

Changes since 04-Jan-2002:

* Added the <tt>ppp.dummyremoteaddress</tt> and <tt>ppp.ethernet.ac</tt> configuration keywords.

Changes since 10-Dec-2001:

* Converted to HTML format.
* Added the <tt>&lt;devices/sana2.h&gt;</tt> header file to the appendix.
* The memory alignment for the <tt>S2_DMACopyToBuff64</tt> and <tt>S2_DMACopyFromBuff64</tt> hooks refers to bits and not to bytes.

Changes since 19-Nov-2001:

* Added to the list of reserved configuration keywords (<tt>ppp.idletimeout</tt>, <tt>ppp.peeridletimeout</tt>, <tt>ppp.sendid</tt>).
* Renamed the fields of the <tt>Sana2ExtDeviceStats</tt> structure.
* Clarified that the <tt>S2QUAD</tt> type is a big endian integer.
* More clarifications for the <tt>S2_CONNECT</tt>/<tt>S2_DISCONNECT</tt> and <tt>S2_ONLINE</tt>/<tt>S2_OFFLINE</tt> commands.
* Updated the discussion of the <tt>Sana2DeviceQuery.RawMTU</tt> field, clarifying what is included in the the Ethernet <tt>RawMTU</tt>.
* Updated the <tt>&lt;devices/sana2.h&gt;</tt> header file. Note that there is no equivalent <tt>&quot;devices/sana2.i&quot;</tt> header file yet.

Changes since 12-Nov-2001:

* Changed the command numbers of <tt>S2_GETPEERADDRESS</tt>, <tt>S2_GETDNSADDRESS</tt>, <tt>S2_GETEXTENDEDGLOBALSTATS</tt>, <tt>S2_CONNECT</tt>, <tt>S2_DISCONNECT</tt> and <tt>S2_SAMPLE_THROUGHPUT</tt> to be NSD-compliant. Also assigned a new number to the <tt>S2_SAMPLE_THROUGHPUT </tt> command.
* Added section 4 (&quot;Extensions for existing commands&quot;).
* Added the last paragraph to section 9, relating to the future extension of the <tt>Sana2DeviceQuery</tt> structure.

Changes since 03-Nov-2001:

* Renamed <tt>S2_GETNEWGLOBALSTATS</tt> to <tt>S2_GETEXTENDEDGLOBALSTATS</tt> (see [[#2.3|section 2.3]]).
* The <tt>S2_GETEXTENDEDGLOBALSTATS</tt> now uses 64 bit quantities for the <tt>s2xds_PacketsReceived</tt>, <tt>s2xds_PacketsSent</tt>, <tt>s2xds_BadData</tt>, <tt>s2xds_Overruns</tt>, <tt>s2xds_UnknownTypesReceived</tt> and <tt>s2xds_Reconfigurations</tt> counters.
* In section 2.6 the <tt>S2_SAMPLE_THROUGHPUT</tt> command was modified to use 64 bit integers for all members of the <tt>Sana2ThroughputStats</tt> structure.
* All proposed commands are now listed with their numeric IDs.
* The command autodocs specifically mention the <tt>ios2_BufferManagement</tt> field.
* All references to <tt>ios2_Error</tt> have been replaced with <tt>ios2_Req.io_Error</tt>.
* In section 7 the use of the <tt>IOERR_NOCMD</tt>/<tt>S2ERR_NOT_SUPPORTED</tt> error codes is clarified.
* Section 7 takes a more detailed look at safe default values returned by the query commands.
* Inserted section 3 (&quot;Annotations for existing commands&quot;).
* The <tt>S2_CONNECT</tt> and <tt>S2_DISCONNECT</tt> commands now specifically mention the life time of the data they have to deal with.

Changes since 14-Oct-2001:

* Added <tt>S2_CONNECT</tt> and <tt>S2_DISCONNECT</tt> commands.
* Added <tt>S2EVENT_CONNECT</tt> and <tt>S2EVENT_DISCONNECT</tt> events.
* Added section 4 (&quot;New wire error codes&quot;).
* In section 5.2 the originally proposed log callback function has been wrapped into a standard Hook structure.
* Removed item on <tt>S2_CONNECT</tt>/<tt>S2_DISCONNECT</tt> from section 7 (&quot;Unsolved problems&quot;).
* Added section 8 (&quot;Changes&quot;).

== Revision 5 ==

22-Mar-2004:

* Conversion to HTML

21-Jan-2004:

* Added clarifications for <tt>schm_MsgSize</tt>, <tt>slhm_MsgSize</tt>, <tt>slhm_Priority</tt>, <tt>slhm_Name</tt>, <tt>slhm_Message</tt> and <tt>s2h_Hook</tt>.
* Updated the <tt>S2_SANA2HOOK</tt> documentation.
* Shortened section 4.
* Updated section 5.

30-Nov-2003:

* Extended the applicability of the <tt>Hook</tt> to logging.
* Renamed <tt>S2_COPYHOOK</tt> to <tt>S2_SANA2HOOK</tt>, which matches the extended scope it should cover.
* The message passed to the <tt>Hook</tt> now includes a size field which holds the size of the message, expressed in bytes.

06-Oct-2003:

* Replaced the list of new tag items with a single <tt>Hook</tt>, which is installed through a new command.
* Added &quot;Caveats&quot; and &quot;Changes&quot; sections
* Removed the documentation for the originally proposed <tt>TagItem</tt> (sections 3.2.1 through 3.2.10)

= History =

== Revision 2 and 3 ==

This standard has undergone several drafts with long periods for comment from developers and the Amiga community at large. These drafts include a UseNet release which was also distributed on the Fish Disks in June, 1991 (as well as published in the '91 DevCon notes), and the November 7 Draft for Final Comment and Approval distributed via Bix, ADSP and UseNet. There were also several intermediate drafts with more limited distribution.

== Revision 4 ==

(Olaf Barthel)

I've been working on a TCP/IP stack and PPP drivers to go with them for a while and found that there were some things that SANA-II did specifically not address, and which ought to be covered by it. In other areas clarification was needed. Also, discussions I had with Harald Frank suggested that the extensions made by Heinz Wrobel and Stefan Sticht in the SANA-IIR3 specification could need extending.

