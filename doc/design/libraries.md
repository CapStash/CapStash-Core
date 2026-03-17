# Libraries

| Name                     | Description |
|--------------------------|-------------|
| *libCapStash_cli*         | RPC client functionality used by *CapStash-cli* executable |
| *libCapStash_common*      | Home for common functionality shared by different executables and libraries. Similar to *libCapStash_util*, but higher-level (see [Dependencies](#dependencies)). |
| *libCapStash_consensus*   | Stable, backwards-compatible consensus functionality used by *libCapStash_node* and *libCapStash_wallet* and also exposed as a [shared library](../shared-libraries.md). |
| *libCapStashconsensus*    | Shared library build of static *libCapStash_consensus* library |
| *libCapStash_kernel*      | Consensus engine and support library used for validation by *libCapStash_node* and also exposed as a [shared library](../shared-libraries.md). |
| *libCapStashqt*           | GUI functionality used by *CapStash-qt* and *CapStash-gui* executables |
| *libCapStash_ipc*         | IPC functionality used by *CapStash-node*, *CapStash-wallet*, *CapStash-gui* executables to communicate when [`--enable-multiprocess`](multiprocess.md) is used. |
| *libCapStash_node*        | P2P and RPC server functionality used by *CapStashd* and *CapStash-qt* executables. |
| *libCapStash_util*        | Home for common functionality shared by different executables and libraries. Similar to *libCapStash_common*, but lower-level (see [Dependencies](#dependencies)). |
| *libCapStash_wallet*      | Wallet functionality used by *CapStashd* and *CapStash-wallet* executables. |
| *libCapStash_wallet_tool* | Lower-level wallet functionality used by *CapStash-wallet* executable. |
| *libCapStash_zmq*         | [ZeroMQ](../zmq.md) functionality used by *CapStashd* and *CapStash-qt* executables. |

## Conventions

- Most libraries are internal libraries and have APIs which are completely unstable! There are few or no restrictions on backwards compatibility or rules about external dependencies. Exceptions are *libCapStash_consensus* and *libCapStash_kernel* which have external interfaces documented at [../shared-libraries.md](../shared-libraries.md).

- Generally each library should have a corresponding source directory and namespace. Source code organization is a work in progress, so it is true that some namespaces are applied inconsistently, and if you look at [`libCapStash_*_SOURCES`](../../src/Makefile.am) lists you can see that many libraries pull in files from outside their source directory. But when working with libraries, it is good to follow a consistent pattern like:

  - *libCapStash_node* code lives in `src/node/` in the `node::` namespace
  - *libCapStash_wallet* code lives in `src/wallet/` in the `wallet::` namespace
  - *libCapStash_ipc* code lives in `src/ipc/` in the `ipc::` namespace
  - *libCapStash_util* code lives in `src/util/` in the `util::` namespace
  - *libCapStash_consensus* code lives in `src/consensus/` in the `Consensus::` namespace

## Dependencies

- Libraries should minimize what other libraries they depend on, and only reference symbols following the arrows shown in the dependency graph below:

<table><tr><td>

```mermaid

%%{ init : { "flowchart" : { "curve" : "basis" }}}%%

graph TD;

CapStash-cli[CapStash-cli]-->libCapStash_cli;

CapStashd[CapStashd]-->libCapStash_node;
CapStashd[CapStashd]-->libCapStash_wallet;

CapStash-qt[CapStash-qt]-->libCapStash_node;
CapStash-qt[CapStash-qt]-->libCapStashqt;
CapStash-qt[CapStash-qt]-->libCapStash_wallet;

CapStash-wallet[CapStash-wallet]-->libCapStash_wallet;
CapStash-wallet[CapStash-wallet]-->libCapStash_wallet_tool;

libCapStash_cli-->libCapStash_util;
libCapStash_cli-->libCapStash_common;

libCapStash_common-->libCapStash_consensus;
libCapStash_common-->libCapStash_util;

libCapStash_kernel-->libCapStash_consensus;
libCapStash_kernel-->libCapStash_util;

libCapStash_node-->libCapStash_consensus;
libCapStash_node-->libCapStash_kernel;
libCapStash_node-->libCapStash_common;
libCapStash_node-->libCapStash_util;

libCapStashqt-->libCapStash_common;
libCapStashqt-->libCapStash_util;

libCapStash_wallet-->libCapStash_common;
libCapStash_wallet-->libCapStash_util;

libCapStash_wallet_tool-->libCapStash_wallet;
libCapStash_wallet_tool-->libCapStash_util;

classDef bold stroke-width:2px, font-weight:bold, font-size: smaller;
class CapStash-qt,CapStashd,CapStash-cli,CapStash-wallet bold
```
</td></tr><tr><td>

**Dependency graph**. Arrows show linker symbol dependencies. *Consensus* lib depends on nothing. *Util* lib is depended on by everything. *Kernel* lib depends only on consensus and util.

</td></tr></table>

- The graph shows what _linker symbols_ (functions and variables) from each library other libraries can call and reference directly, but it is not a call graph. For example, there is no arrow connecting *libCapStash_wallet* and *libCapStash_node* libraries, because these libraries are intended to be modular and not depend on each other's internal implementation details. But wallet code is still able to call node code indirectly through the `interfaces::Chain` abstract class in [`interfaces/chain.h`](../../src/interfaces/chain.h) and node code calls wallet code through the `interfaces::ChainClient` and `interfaces::Chain::Notifications` abstract classes in the same file. In general, defining abstract classes in [`src/interfaces/`](../../src/interfaces/) can be a convenient way of avoiding unwanted direct dependencies or circular dependencies between libraries.

- *libCapStash_consensus* should be a standalone dependency that any library can depend on, and it should not depend on any other libraries itself.

- *libCapStash_util* should also be a standalone dependency that any library can depend on, and it should not depend on other internal libraries.

- *libCapStash_common* should serve a similar function as *libCapStash_util* and be a place for miscellaneous code used by various daemon, GUI, and CLI applications and libraries to live. It should not depend on anything other than *libCapStash_util* and *libCapStash_consensus*. The boundary between _util_ and _common_ is a little fuzzy but historically _util_ has been used for more generic, lower-level things like parsing hex, and _common_ has been used for CapStash-specific, higher-level things like parsing base58. The difference between util and common is mostly important because *libCapStash_kernel* is not supposed to depend on *libCapStash_common*, only *libCapStash_util*. In general, if it is ever unclear whether it is better to add code to *util* or *common*, it is probably better to add it to *common* unless it is very generically useful or useful particularly to include in the kernel.


- *libCapStash_kernel* should only depend on *libCapStash_util* and *libCapStash_consensus*.

- The only thing that should depend on *libCapStash_kernel* internally should be *libCapStash_node*. GUI and wallet libraries *libCapStashqt* and *libCapStash_wallet* in particular should not depend on *libCapStash_kernel* and the unneeded functionality it would pull in, like block validation. To the extent that GUI and wallet code need scripting and signing functionality, they should be get able it from *libCapStash_consensus*, *libCapStash_common*, and *libCapStash_util*, instead of *libCapStash_kernel*.

- GUI, node, and wallet code internal implementations should all be independent of each other, and the *libCapStashqt*, *libCapStash_node*, *libCapStash_wallet* libraries should never reference each other's symbols. They should only call each other through [`src/interfaces/`](`../../src/interfaces/`) abstract interfaces.

## Work in progress

- Validation code is moving from *libCapStash_node* to *libCapStash_kernel* as part of [The libCapStashkernel Project #24303](https://github.com/CapStash/CapStash/issues/24303)
- Source code organization is discussed in general in [Library source code organization #15732](https://github.com/CapStash/CapStash/issues/15732)
