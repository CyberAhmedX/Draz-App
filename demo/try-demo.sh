#!/bin/sh
# Runs Draz with the sample accounts, products and orders in the demo-data folder.
cd "$(dirname "$0")" || exit 1
./draz --data demo-data
