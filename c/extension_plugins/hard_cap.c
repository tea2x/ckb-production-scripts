#define CKB_C_STDLIB_PRINTF
#include <stdio.h>

#include "blockchain.h"
#include "ckb_syscalls.h"

// We will leverage gcc's 128-bit integer extension here for number crunching.
typedef unsigned __int128 uint128_t;

// Common error codes that might be returned by the script.
#define ERROR_NO_HARDCAP_CELL 1
#define ERROR_CAN_NOT_MINT_OVER_HARDCAP 2
#define ERROR_WRONG_HARDCAP_CALCULATION 3

// The modes of operation for the script. 
enum Mode
{
	Burn, // Consume an existing counter cell.
	Create, // Create a new counter cell.
	Transfer, // Transfer (update) a counter cell and increase its value.
};

// sum xudt amount
static uint128_t sumXudt(size_t source) {
    uint128_t output_amount = 0;
    size_t counter = 0;
    uint64_t len = 16;
    while (1) {
        uint128_t current_amount = 0;
        int ret = ckb_load_cell_data((uint8_t *)&current_amount, &len, 0, counter, source);
        if (ret == CKB_INDEX_OUT_OF_BOUND) {
            break;
        }
        output_amount += current_amount;
        counter += 1;
    }
    return output_amount;
}
// Determines the mode of operation for the currently executing script.
static uint8_t determineMode() {
	// Gather counts on the number of group input and groupt output cells.
	uint128_t inputAmountCount = sumXudt(CKB_SOURCE_GROUP_INPUT);
	uint128_t outputAmountCount = sumXudt(CKB_SOURCE_GROUP_OUTPUT);

	// Detect the operation based on the cell count.
	if (inputAmountCount > outputAmountCount) {
		return Burn;
	}
	if (inputAmountCount < outputAmountCount) {
		return Create;
	}
	return Transfer;
}

static size_t findHardCappedCell(uint8_t *codeHashPtr, size_t source) {
    int ret = 0;
    size_t current = 0;
    while (current < SIZE_MAX) {
        uint64_t len = 32;
        uint8_t hash[32];
        ret = ckb_load_cell_by_field(hash, &len, 0, current, source, CKB_CELL_FIELD_TYPE_HASH);
        switch (ret) {
            case CKB_ITEM_MISSING:
                break;
            case CKB_SUCCESS:
                if (memcmp(codeHashPtr, hash, 32) == 0) {
                    /* Found a match */
                    printf("found a match, input index of the type ID is: %llu", current);
                    return current;
                }
                break;
            default:
                return ERROR_NO_HARDCAP_CELL;
        }
        current++;
    }
    return ERROR_NO_HARDCAP_CELL;
}

__attribute__((visibility("default"))) int validate(int owner_mode, uint32_t i, uint8_t * args_ptr, uint32_t args_size) {
    /* with sudt, owner mode is god mode and is CREATION/BURN mode.
       With extension1 we're saying that its not a god-CREATION mode anymore and coin creation must be restricted.
       Now in owner_mode + CREATION, it must go through the following validation.
    */ 
    enum Mode operationMode = determineMode();
    if (!(owner_mode && operationMode == Create))
        return CKB_SUCCESS;

    // find the hardcap(a typeId) cell in the inputs
    uint32_t oldHardcap = 0;
    uint64_t len = 4;
    size_t findRet;
    findRet = findHardCappedCell(args_ptr, CKB_SOURCE_INPUT);
    if (findRet == ERROR_NO_HARDCAP_CELL)
        return findRet;
    ckb_load_cell_data((uint8_t *)&oldHardcap, &len, 0, findRet, CKB_SOURCE_INPUT);
    printf(">>> oldHardcap is: %d", oldHardcap);

    // find the hardcap(a typeId) cell in the outputs
    uint32_t newHardcap = 0;
    findRet = findHardCappedCell(args_ptr, CKB_SOURCE_OUTPUT);
    if (findRet == ERROR_NO_HARDCAP_CELL)
        return findRet;
    ckb_load_cell_data((uint8_t *)&newHardcap, &len, 0, findRet, CKB_SOURCE_OUTPUT);
    printf(">>> newHardcap is: %d", newHardcap);

    // fetch and sum all xudt amount in the outputs
    uint128_t output_amount = sumXudt(CKB_SOURCE_GROUP_OUTPUT);
    
    // validate hardcap rules
    if (output_amount > (uint128_t)oldHardcap)
        return ERROR_CAN_NOT_MINT_OVER_HARDCAP;
    
    if ((uint128_t)newHardcap != ((uint128_t)oldHardcap - output_amount))
        return ERROR_WRONG_HARDCAP_CALCULATION;

    return CKB_SUCCESS;
}
