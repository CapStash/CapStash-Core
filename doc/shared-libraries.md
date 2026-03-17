Shared Libraries
================

## CapStashconsensus
***This library is deprecated and will be removed in v28***

The purpose of this library is to make the verification functionality that is critical to CapStash's consensus available to other applications, e.g. to language bindings.

### API

The interface is defined in the C header `CapStashconsensus.h` located in `src/script/CapStashconsensus.h`.

#### Version

`CapStashconsensus_version` returns an `unsigned int` with the API version *(currently `2`)*.

#### Script Validation

`CapStashconsensus_verify_script`, `CapStashconsensus_verify_script_with_amount` and `CapStashconsensus_verify_script_with_spent_outputs` return an `int` with the status of the verification. It will be `1` if the input script correctly spends the previous output `scriptPubKey`.

##### Parameters
###### CapStashconsensus_verify_script
- `const unsigned char *scriptPubKey` - The previous output script that encumbers spending.
- `unsigned int scriptPubKeyLen` - The number of bytes for the `scriptPubKey`.
- `const unsigned char *txTo` - The transaction with the input that is spending the previous output.
- `unsigned int txToLen` - The number of bytes for the `txTo`.
- `unsigned int nIn` - The index of the input in `txTo` that spends the `scriptPubKey`.
- `unsigned int flags` - The script validation flags *(see below)*.
- `CapStashconsensus_error* err` - Will have the error/success code for the operation *(see below)*.

###### CapStashconsensus_verify_script_with_amount
- `const unsigned char *scriptPubKey` - The previous output script that encumbers spending.
- `unsigned int scriptPubKeyLen` - The number of bytes for the `scriptPubKey`.
- `int64_t amount` - The amount spent in the input
- `const unsigned char *txTo` - The transaction with the input that is spending the previous output.
- `unsigned int txToLen` - The number of bytes for the `txTo`.
- `unsigned int nIn` - The index of the input in `txTo` that spends the `scriptPubKey`.
- `unsigned int flags` - The script validation flags *(see below)*.
- `CapStashconsensus_error* err` - Will have the error/success code for the operation *(see below)*.

###### CapStashconsensus_verify_script_with_spent_outputs
- `const unsigned char *scriptPubKey` - The previous output script that encumbers spending.
- `unsigned int scriptPubKeyLen` - The number of bytes for the `scriptPubKey`.
- `int64_t amount` - The amount spent in the input
- `const unsigned char *txTo` - The transaction with the input that is spending the previous output.
- `unsigned int txToLen` - The number of bytes for the `txTo`.
- `UTXO *spentOutputs` - Previous outputs spent in the transaction. `UTXO` is a struct composed by `const unsigned char *scriptPubKey`, `unsigned int scriptPubKeySize` (the number of bytes for the `scriptPubKey`) and `unsigned int value`.
- `unsigned int spentOutputsLen` - The number of bytes for the `spentOutputs`.
- `unsigned int nIn` - The index of the input in `txTo` that spends the `scriptPubKey`.
- `unsigned int flags` - The script validation flags *(see below)*.
- `CapStashconsensus_error* err` - Will have the error/success code for the operation *(see below)*.

##### Script Flags
- `CapStashconsensus_SCRIPT_FLAGS_VERIFY_NONE`
- `CapStashconsensus_SCRIPT_FLAGS_VERIFY_P2SH` - Evaluate P2SH ([BIP16](https://github.com/CapStash/bips/blob/master/bip-0016.mediawiki)) subscripts
- `CapStashconsensus_SCRIPT_FLAGS_VERIFY_DERSIG` - Enforce strict DER ([BIP66](https://github.com/CapStash/bips/blob/master/bip-0066.mediawiki)) compliance
- `CapStashconsensus_SCRIPT_FLAGS_VERIFY_NULLDUMMY` - Enforce NULLDUMMY ([BIP147](https://github.com/CapStash/bips/blob/master/bip-0147.mediawiki))
- `CapStashconsensus_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY` - Enable CHECKLOCKTIMEVERIFY ([BIP65](https://github.com/CapStash/bips/blob/master/bip-0065.mediawiki))
- `CapStashconsensus_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY` - Enable CHECKSEQUENCEVERIFY ([BIP112](https://github.com/CapStash/bips/blob/master/bip-0112.mediawiki))
- `CapStashconsensus_SCRIPT_FLAGS_VERIFY_WITNESS` - Enable WITNESS ([BIP141](https://github.com/CapStash/bips/blob/master/bip-0141.mediawiki))
- `CapStashconsensus_SCRIPT_FLAGS_VERIFY_TAPROOT` - Enable TAPROOT ([BIP340](https://github.com/CapStash/bips/blob/master/bip-0340.mediawiki), [BIP341](https://github.com/CapStash/bips/blob/master/bip-0341.mediawiki), [BIP342](https://github.com/CapStash/bips/blob/master/bip-0342.mediawiki))

##### Errors
- `CapStashconsensus_ERR_OK` - No errors with input parameters *(see the return value of `CapStashconsensus_verify_script` for the verification status)*
- `CapStashconsensus_ERR_TX_INDEX` - An invalid index for `txTo`
- `CapStashconsensus_ERR_TX_SIZE_MISMATCH` - `txToLen` did not match with the size of `txTo`
- `CapStashconsensus_ERR_DESERIALIZE` - An error deserializing `txTo`
- `CapStashconsensus_ERR_AMOUNT_REQUIRED` - Input amount is required if WITNESS is used
- `CapStashconsensus_ERR_INVALID_FLAGS` - Script verification `flags` are invalid (i.e. not part of the libconsensus interface)
- `CapStashconsensus_ERR_SPENT_OUTPUTS_REQUIRED` - Spent outputs are required if TAPROOT is used
- `CapStashconsensus_ERR_SPENT_OUTPUTS_MISMATCH` - Spent outputs size doesn't match tx inputs size

### Example Implementations
- [NCapStash](https://github.com/MetacoSA/NCapStash/blob/5e1055cd7c4186dee4227c344af8892aea54faec/NCapStash/Script.cs#L979-#L1031) (.NET Bindings)
- [node-libCapStashconsensus](https://github.com/bitpay/node-libCapStashconsensus) (Node.js Bindings)
- [java-libCapStashconsensus](https://github.com/dexX7/java-libCapStashconsensus) (Java Bindings)
- [CapStashconsensus-php](https://github.com/Bit-Wasp/CapStashconsensus-php) (PHP Bindings)
- [rust-CapStashconsensus](https://github.com/rust-CapStash/rust-CapStashconsensus) (Rust Bindings)