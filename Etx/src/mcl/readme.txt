To install the MCL driver, put the following files in a directory:
        mclmp.inf
        mcltp.inf
        mcl.sys
        mcl.exe
(The mkkit.vbs script can be used to do this automatically.)

Then you need to install the miniport (using mclmp.inf) and install
the transport (using mcltp.inf). You can do this in either order.

To programmatically install both the miniport and transport, you can
use the mcl.exe program. Use "mcl.exe install <dirpath>"
where <dirpath> is the full path of the directory with the above files.
For example, c:\mclkit.  Don't forget the <dirpath>, or you'll get a
cryptic error.

After installing MCL, you will see the Mesh Virtual Adapter show up
in the Network Connections folder. You can configure IPv4 and IPv6
to use this virtual adapter the same as any real physical adapter.
You will then have IP connectivity over the mesh.
Use ipconfig.exe to check IP address assignment.

To upgrade to a new version of mcl.sys, you can Disable the miniport
(right-click on the Mesh Virtual Adapter in Network Connections), copy a new
mcl.sys to %windir%\system32\drivers, then Enable the miniport.
Rebooting is not necessary. Note that if the private ioctls have changed,
you will also need a matching mcl.exe.
(The update.vbs script can be used to do this automatically.)

You can also use mcl.exe to disable/enable the miniport,
using "mcl.exe disable" and "mcl.exe enable", or
to do both use "mcl.exe restart".

To manually install the miniport:
Open the Add Hardware Wizard (Control Panel / Add Hardware).
Click Next.
Wait while it searches.
When it asks if the hardware is connected, select "Yes" then Next.
Scroll down to the bottom, select "Add a new hardware device" then Next.
Select "Install the hardware that I manually select" then Next.
Select "Network Adapters" then Next.
Wait while it searches.
Click on Have Disk.
Browse to the directory where you put the MCL installation files.
Select "MCL (Mesh Connectivity Layer) Virtual Adapter" then Next.

To manually install the protocol:
Open the Network Connections folder in Control Panel.
Right-click on any one of your adapters and choose Properties.
In the General tab, click on Install.
Select Protocol then Add.
Wait while it searches.
Click on Have Disk.
Browse to the directory where you put the MCL installation files.
Select "MCL (Mesh Connectivity Layer) Protocol" then OK.

The LQSR (Link-Quality Source Routing) protocol is implemented inside MCL.
To install the LQSR netmon parser:
Install netmon if necessary.
Go to netmon's directory, eg \Program Files\Microsoft Network Monitor.
Copy lqsr.dll to the Parsers subdirectory.
Edit netmon.ini and add this line to the [ETYPES] section:
ETYPE_MSFT          =LQSR,Link-Quality Source Routing,                886F
Edit parser.ini and add this line to the [PARSERS] section:
    LQSR.DLL    = 0: LQSR
And also add this section to parser.ini:
[LQSR]
    Comment     = "Link-Quality Source Routing"
    FollowSet   =
    HelpFile    =
Edit Parsers\mac.ini and add this line to the [ETYPES] section:
0x886F  =   LQSR


A summary of the major mcl.exe options:

mcl install <dir>
    Installs the MCL driver using files in the specified directory
    and enables the MCL driver.
mcl enable
    Enables the MCL driver, causing it to be loaded and start running.
mcl disable
    Disables the MCL driver, causing it to stop running and unload.
mcl restart
    Disables then enables the MCL driver.
mcl [-v] va
    Prints information & statistics about the virtual adapter.
    This is the first command to use to see if MCL is running properly.
mcl [-v] lc
    Prints information & statistics about the link cache.
    This is the second command to use to see if MCL is running properly.
    It will tell you what other nodes this node knows about.
mcl lcf
    Flushes the link cache so MCL forgets everything it knows
    about other nodes and links.
mcl sr <dest>
    Prints the source route that will be used to reach the destination.
    This queries the link cache for a route but does NOT do Route Discovery.
    The destination should be the MCL address assigned to another node.
    Run "mcl va" on the other node to get its address.

Note that nodes must have the same crypto configuration, metric type, and
version number (see "mcl va") in order to communicate.

Registry configuration:

The registry configuration keys for MCL are located in the usual place
for network adapter configuration. That is,
HKLM\System\CurrentControlSet\Control\Class\{4D36E972-E325-11CE-BFC1-08002BE10318}
Then under there look for the adapter with ComponentId ms_mclmp.

The registry keys of interest:

NetworkAddress REG_SZ This is the MCL address of the node.
        It is automatically configured when MCL first runs,
        but you can edit it (and restart MCL) to change it.

Most registry keys live in the Parameters subkey.
Use "mcl -p vac" to configure them.

Both CryptoKeyMAC and CryptoKeyAES must be configured for MCL's
cryptography to be enabled. ("mcl va" will show if crypto is disabled.)
Note that if cryptography is enabled, then the netmon parser will
not be able to parse the payloads (IP headers etc) of the LQSR packets.
