#!/bin/bash                                                                         
# parse_all_references.sh
# Usage: ./parse_all_references.sh /path/to/pdf/folder
                                                                                
SIOYEK="/Applications/sioyek.app/Contents/MacOS/sioyek"
PDF_FOLDER="${1:-.}"  # Default to current directory

find "$PDF_FOLDER" -name "*.pdf" -type f | while read -r pdf; do
    echo "Processing: $pdf"

    "$SIOYEK" "$pdf" --execute-command parse_references

    # Small delay to let grobid finish
    sleep 2
done

echo "Done processing all PDFs"