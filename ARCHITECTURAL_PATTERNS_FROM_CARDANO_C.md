# Architectural Patterns from Cardano-C Library

Extracted design principles and patterns applicable to custom blockchain solutions. **NOT Cardano-specific functionality** — focus on transferable architectural principles.

---

## 1. CODE ORGANIZATION

### Header Hierarchy
- **Structure**: Nested include directories by domain (`include/cardano/address/`, `include/cardano/crypto/`, etc.)
- **Pattern**: One logical domain per directory with multiple related headers
- **Benefit**: Scales well with codebase growth, reduces namespace pollution

### Module Separation Strategy
- **Domain-based grouping**: Rather than technical layers, organize by business domains
  - `address/` - Address handling (parsing, validation, generation)
  - `crypto/` - Cryptographic operations
  - `transaction/` - Transaction types and operations
  - `serialization/` - CBOR encoding/decoding
  - `builder/` - Complex object construction
  - `common/` - Shared utilities (buffers, errors, types)

### File Naming Conventions
- **Pattern**: `lowercase_with_underscores`
- **Examples**: `cbor_writer.h`, `tx_builder.h`, `error.h`
- **Benefits**: Cross-platform compatibility, consistency, readability

### Include Guard Pattern
```c
#ifndef BIGLUP_LABS_INCLUDE_CARDANO_MODULE_NAME_H
#define BIGLUP_LABS_INCLUDE_CARDANO_MODULE_NAME_H

/* C++ extern guard */
#ifdef __cplusplus
extern "C" {
#endif

/* Public API declarations */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* BIGLUP_LABS_INCLUDE_CARDANO_MODULE_NAME_H */
```
- **Note**: Includes company prefix to avoid collisions

---

## 2. API DESIGN PATTERNS

### Object Lifecycle Management (Birth → Death)

**Constructor Pattern**: `type_new()`
```c
cardano_cbor_writer_t* writer = cardano_cbor_writer_new();
/* - Allocates object
 * - Initializes reference count to 1
 * - Caller owns the reference
 */
```

**Destructor Pattern**: `type_unref()`
```c
void cardano_cbor_writer_unref(cardano_cbor_writer_t** writer);
/* - Takes pointer to pointer
 * - Decrements reference count
 * - Sets pointer to NULL (prevents dangling pointers)
 * - Deallocates when count reaches zero
 */
```

**Reference Count Getter**: `type_refcount()`
- Returns current reference count (useful for debugging)
- Returns `size_t`

### Reference Counting Memory Model

**Design Principle**: Enable integration with multiple memory models (GC, manual, RAII, etc.)

```c
/* Ownership Transfer Pattern */
cardano_object_t* obj = cardano_object_new();      /* refcount = 1 */

/* Sharing ownership */
cardano_object_t* ref = obj;
cardano_object_ref(ref);                            /* refcount = 2 */

/* Releasing references */
cardano_object_unref(&obj);                         /* refcount = 1 */
cardano_object_unref(&ref);                         /* refcount = 0, deallocated */
```

**Key Rule**: Getter functions increment reference count
- Caller must call `_unref()` on results from getter functions
- This prevents premature deallocation by another thread

### Error Handling Pattern

**Unified Error Type**: `cardano_error_t` (enum)
```c
typedef enum {
  CARDANO_SUCCESS = 0,
  CARDANO_ERROR_INSUFFICIENT_BUFFER_SIZE = 1,
  CARDANO_ERROR_POINTER_IS_NULL = 2,
  CARDANO_ERROR_INVALID_CBOR_VALUE = 301,
  CARDANO_ERROR_INVALID_ADDRESS_TYPE = 400,
  /* ... domain-specific errors ... */
} cardano_error_t;
```

**Convention**:
- All operations returning status use `cardano_error_t`
- `CARDANO_SUCCESS = 0` for backward compatibility
- Error space organized by domain:
  - 0-99: General errors
  - 200-299: Crypto errors
  - 300-399: CBOR/serialization errors
  - 400-499: Address errors
  - 1000+: High-level operation errors

**Error Conversion**: `cardano_error_to_string(error)`
- Implements human-readable error messages
- Essential for user-facing error reporting

**Last Error Pattern**:
```c
/* Store detailed error messages */
void cardano_object_set_last_error(cardano_object_t* obj, const char* message);
const char* cardano_object_get_last_error(const cardano_object_t* obj);
```
- Complements numeric error codes with descriptive context
- Limited to 1024 characters (truncates if exceeds)

### Function Naming Conventions

**Pattern**: `[domain_]type_action`

```c
/* Constructors */
cardano_address_t* cardano_address_from_string(...);
cardano_address_t* cardano_address_from_bytes(...);

/* Conversions */
cardano_base_address_t* cardano_address_to_base_address(...);

/* Queries/Getters */
cardano_address_type_t* result = cardano_address_get_type(...);
size_t size = cardano_address_get_string_size(...);
const char* str = cardano_address_get_string(...);  /* No copy */

/* Setters */
void cardano_tx_builder_set_utxos(cardano_tx_builder_t* builder, ...);

/* Predicates */
bool cardano_address_is_valid_bech32(const char* data, size_t size);

/* Serialization */
cardano_error_t cardano_object_to_bytes(const cardano_object_t*, byte_t*, size_t);
cardano_error_t cardano_object_encode(cardano_object_t*, byte_t*, size_t);
```

### Input/Output Parameter Convention

**Pointer Semantics**:
```c
/* [in] - Input parameter (read-only) */
void func1(const type_t* input);

/* [out] - Output parameter (caller allocates) */
void func2(type_t* output);

/* [in,out] - Modified by function */
void func3(type_t* inout);

/* [out] - Double pointer for allocation by function */
cardano_error_t func4(type_t** allocated_by_func);
```

---

## 3. TYPE SYSTEM

### Opaque Type Declaration

**Header File**: Forward declaration only
```c
/* No structure definition exposed */
typedef struct cardano_cbor_writer_t cardano_cbor_writer_t;
```

**Benefits**:
- Isolates implementation details (ABI stability)
- Prevents accidental direct member access
- Enables API evolution without breaking binaries
- Forces use of accessor functions (maintains invariants)

### Data Type Definitions

**Integer Types** (never use `int`, `unsigned long`, etc.):
```c
#include <stdint.h>

uint8_t    /* unsigned 8-bit */
int16_t    /* signed 16-bit */
uint64_t   /* unsigned 64-bit */
size_t     /* platform-dependent size type */
```

**Primitive Wrappers**:
```c
typedef uint8_t  byte_t;     /* Raw bytes */
typedef uint8_t  bool;       /* Boolean (0 = false, 1 = true) */
```

### Enumeration Structure

**Naming & Capitalization**:
```c
typedef enum {
  CARDANO_ADDRESS_TYPE_BYRON = 0,        /* UPPERCASE */
  CARDANO_ADDRESS_TYPE_BASE = 1,
  CARDANO_ADDRESS_TYPE_ENTERPRISE = 2,
  /* ... */
} cardano_address_type_t;                /* _t suffix on typedef */
```

**Pattern**:
- Enum name: lowercase with underscores
- Members: PREFIX + UPPERCASE_NAME
- Typedef: adds `_t` suffix

### Struct Pattern

**Three Declaration Styles**:

1. **Named struct only** (no typedef):
```c
struct my_struct {
  int a;
  char* b;
};
```

2. **Anonymous struct with typedef** (preferred for opaque types):
```c
typedef struct {
  int a;
  char* b;
} my_struct_t;
```

3. **Named struct with typedef** (for type systems):
```c
typedef struct my_struct {
  int a;
  char* b;
} my_struct_t;
```

**Initialization Convention** (C99 designated initializers):
```c
my_struct_t s = {
  .a = 1,
  .b = "value",   /* Trailing comma for consistency */
};
```

---

## 4. TRANSACTION BUILDING PATTERN

### Builder Pattern Implementation

**Core Characteristics**: Fluent API with state accumulation

```c
/* 1. Create builder */
cardano_tx_builder_t* builder = cardano_tx_builder_new();

/* 2. Configure via setters (can be chained logically) */
cardano_tx_builder_set_utxos(builder, utxo_list);
cardano_tx_builder_set_change_address(builder, address);
cardano_tx_builder_set_invalid_after(builder, timestamp);

/* 3. Add operations (e.g., transaction outputs) */
cardano_tx_builder_send_lovelace(builder, dest_addr, amount);

/* 4. Build (validates and produces output) */
cardano_transaction_t* tx = NULL;
cardano_error_t result = cardano_tx_builder_build(builder, &tx);

/* 5. Cleanup */
cardano_tx_builder_unref(&builder);
```

**Design Principles**:
- **Single Responsibility**: Builder owns state during construction
- **Immutable Result**: Once built, transaction is immutable
- **Validation Deferred**: Checking happens at build time (batch errors)
- **Error Accumulation**: Builder captures errors internally
- **Error Context**: `cardano_tx_builder_get_last_error()` provides details

### Input/Output Handling Structure

**Pattern**: Lists with accessors

```c
/* Inputs (UTxOs/coins to spend) */
cardano_tx_builder_set_utxos(builder, utxo_list);

/* Outputs (destinations and amounts) */
cardano_tx_builder_send_lovelace(builder, addr, amount);

/* Configuration */
cardano_tx_builder_set_fee(builder, fee);
cardano_tx_builder_set_change_address(builder, change_addr);
```

**Constraint Handling**:
- Coin selection algorithm validates compatibility
- Automatic fee calculation based on transaction size
- Change address for UTXO return

### Configuration Flow

**Sequential Setup** (must follow logical order):
1. Define inputs (UTxOs)
2. Set change address
3. Add recipients/outputs
4. Set timeouts/constraints
5. Build

**Rationale**: Later steps may depend on earlier configuration

---

## 5. SERIALIZATION APPROACH

### CBOR Encoding Pattern

**Writer Interface** (Functional CBOR construction):
```c
/* 1. Create writer */
cardano_cbor_writer_t* writer = cardano_cbor_writer_new();

/* 2. Write types */
cardano_cbor_writer_write_uint(writer, 42);
cardano_cbor_writer_write_textstring(writer, "hello", 5);
cardano_cbor_writer_write_bool(writer, true);

/* 3. Complex types */
cardano_cbor_writer_write_start_array(writer, 3);    /* Definite-length */
  /* Write 3 elements */
cardano_cbor_writer_write_start_array(writer, -1);   /* Indefinite-length */
  /* Write elements */
  cardano_cbor_writer_write_end_array(writer);       /* Marks end */

/* 4. Extract result */
size_t required_size = cardano_cbor_writer_get_encode_size(writer);
byte_t* buffer = malloc(required_size);
cardano_cbor_writer_encode(writer, buffer, required_size);

/* 5. Cleanup */
cardano_cbor_writer_unref(&writer);
free(buffer);
```

**CBOR Tags** (Semantic annotations):
```c
/* Attach meaning to next value */
cardano_cbor_writer_write_tag(writer, CBOR_TAG_BIGNUM);
cardano_cbor_writer_write_bytestring(writer, data, size);
```

### Encoding Patterns

**Two-Stage Size Calculation** (prevents allocation errors):

```c
/* Stage 1: Query size */
size_t required_size = cardano_object_get_encode_size(writer);

/* Stage 2: Allocate and encode */
byte_t* buffer = malloc(required_size);
if (buffer) {
    cardano_error_t result = cardano_object_encode(writer, buffer, required_size);
    if (result == CARDANO_SUCCESS) {
        /* Use buffer */
    }
    free(buffer);
}
```

**Multiple Output Formats**:

```c
/* Raw bytes */
cardano_cbor_writer_encode(writer, buffer, size);

/* Auto-allocated buffer */
cardano_buffer_t* buffer = NULL;
cardano_cbor_writer_encode_in_buffer(writer, &buffer);
/* Caller manages buffer lifecycle */

/* Hexadecimal string */
char* hex_string = malloc(cardano_cbor_writer_get_hex_size(writer));
cardano_cbor_writer_encode_hex(writer, hex_string, hex_size);
```

### Encoding vs. Serialization Terminology

**Pattern**:
- **`encode()`** - CBOR-specific encoding operation
- **`to_bytes()`** - Generic serialization (could be CBOR, JSON, etc.)
- **`write_*()`** - Incremental writer operations

### Both Directions Support

**Writer** (Object → Binary):
- `cardano_cbor_writer_t*` - Accumulate and encode
- Suitable for construction

**Reader** (Binary → Object):
- `cardano_cbor_reader_t*` - Deserialize and parse
- Suitable for parsing/validation

---

## 6. ADDITIONAL DESIGN PRINCIPLES

### Const Correctness

```c
/* Accepts immutable reference */
size_t get_size(const cardano_object_t* obj);

/* Optionally: immutable parameters too */
void process(const size_t len);  /* Though less common */

/* Output parameter: pointer can be modified */
void write_result(cardano_object_t* out);

/* Both immutable: neither caller can modify */
void read_immutable(const void* const ptr);
```

### Null Pointer Safety

```c
/* Always compare pointers against NULL */
if (ptr == NULL) { /* Handle error */ }
if (ptr != NULL) { /* Use pointer */ }

/* Not: if (ptr) { } or if (!ptr) { } */
```

### C99 Features Leveraged

- **Designated initializers**: `.field = value`
- **Fixed-width integer types**: `uint8_t`, `int32_t`, etc.
- **Inline functions** (in headers for performance)
- **`bool` type** (via stdbool.h)
- **Variable declarations** (anywhere in block, not just top)

### Extensibility Without Breaking ABI

**Opaque Struct Pattern**:
- Real implementation in `.c` file
- Header only declares type
- New fields can be added to implementation without affecting callers
- Enables binary compatibility across versions

### No Global Mutable State

**Thread-Safety Property**:
- Each object is independent
- No shared global resources
- Thread-safe by design (no synchronization needed)

---

## 7. PRACTICAL IMPLEMENTATION CHECKLIST

Apply these patterns to your blockchain library:

- [ ] **Organization**: Domain-based directory hierarchy
- [ ] **Headers**: Opaque types in headers, implementation details hidden
- [ ] **Lifecycle**: `_new()`, `_ref()`, `_unref()`, `_refcount()` for all objects
- [ ] **Errors**: Unified `error_t` enum with domain-prefixed error codes
- [ ] **Functions**: Document with `[in]`, `[out]`, `[in,out]` parameter tags
- [ ] **Types**: Use `<stdint.h>` types, never raw `int`/`unsigned`
- [ ] **C++**: Include `extern "C"` guards in headers
- [ ] **Builders**: Setter methods + `_build()` validation
- [ ] **Serialization**: Size calculation before allocation, multiple formats
- [ ] **Constants**: Uppercase for macros and enums
- [ ] **Comments**: Doxygen-enabled documentation for all public items
- [ ] **Testing**: Unit tests with Google Test framework
- [ ] **Code Review**: `clang-format` and `clang-tidy` compliance

---

## 8. EXAMPLE ARCHITECTURE FOR YOUR BLOCKCHAIN

Adapting cardano-c patterns to a custom blockchain:

```
include/midnight_blockchain/
├── core/
│   ├── error.h              /* Global error types */
│   ├── types.h              /* Shared type definitions */
│   └── logger.h             /* Logging utilities */
├── ledger/
│   ├── account.h            /* Account structures */
│   ├── transaction.h        /* Transaction types */
│   └── state.h              /* Ledger state */
├── crypto/
│   ├── key_pair.h           /* Cryptographic keys */
│   └── signature.h          /* Signing/verification */
├── serialization/
│   ├── encoder.h            /* Binary encoding */
│   └── decoder.h            /* Binary decoding */
└── builder/
    └── tx_builder.h         /* Transaction construction */
```

Each module follows:
- Opaque types (forward declared only)
- Reference counting lifecycle
- Error handling via unified enum
- Comprehensive Doxygen documentation

