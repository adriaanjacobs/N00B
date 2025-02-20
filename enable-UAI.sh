#!/bin/bash

MSR_REGISTER="0xC0000080"
UAI_BIT="0x100000"

# Function to check if all MSR values are equal and return the common value
get_common_msr_value() {
    local msr=$1
    local values=$(sudo rdmsr -a $msr)
    local common_value=""
    
    while read -r value; do
        if [ -z "$common_value" ]; then
            common_value=$value
        elif [ "$value" != "$common_value" ]; then
            echo "Warning: MSR values are not consistent across all cores. Bailing out." >&2
            exit 1
        fi
    done <<< "$values"
    
    if [ -z "$common_value" ]; then
        echo "Warning: No MSR values were read. Bailing out." >&2
        exit 1
    fi
    
    echo "$common_value"
}

common_value=$(get_common_msr_value $MSR_REGISTER)

# Check if UAI bit is already set
if (( 0x$common_value & $UAI_BIT )); then
    echo "UAI bit is already set. Nothing to do."
    exit 0
fi

updated_value=$(( 0x$common_value | $UAI_BIT ))

sudo wrmsr -a $MSR_REGISTER $updated_value

echo "UAI bit set in MSR $MSR_REGISTER on all cores: $common_value -> $updated_value. "
