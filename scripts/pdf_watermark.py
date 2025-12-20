#!/usr/bin/env python3
"""
PDF Watermarking Script for MegaCustom
Adds diagonal text watermarks to PDF files with random page selection.

Dependencies:
    pip install reportlab PyPDF2

Usage:
    python pdf_watermark.py --input input.pdf --output output.pdf --text "Watermark Text"
    python pdf_watermark.py --input input.pdf --output output.pdf --text "Line 1" --secondary "Line 2"
    python pdf_watermark.py --input input.pdf --output output.pdf --text "Text" --opacity 0.3 --password "secret"
"""

import argparse
import os
import sys
import random
from io import BytesIO

try:
    from reportlab.pdfgen import canvas
    from reportlab.lib.pagesizes import letter
    from reportlab.lib.colors import Color
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.ttfonts import TTFont
except ImportError:
    print("Error: reportlab not installed. Run: pip install reportlab", file=sys.stderr)
    sys.exit(1)

try:
    from PyPDF2 import PdfReader, PdfWriter
except ImportError:
    print("Error: PyPDF2 not installed. Run: pip install PyPDF2", file=sys.stderr)
    sys.exit(1)


def hex_to_rgb(hex_color):
    """Convert hex color to RGB tuple (0-1 range)."""
    hex_color = hex_color.lstrip('#')
    if len(hex_color) == 6:
        r = int(hex_color[0:2], 16) / 255.0
        g = int(hex_color[2:4], 16) / 255.0
        b = int(hex_color[4:6], 16) / 255.0
        return (r, g, b)
    return (0.83, 0.65, 0.38)  # Default golden color #d4a760


def create_watermark_pdf(text, secondary_text="", width=612, height=792,
                         opacity=0.3, angle=45, color="#d4a760",
                         font_size=40, secondary_font_size=32):
    """
    Create a PDF with watermark text at specified angle.

    Args:
        text: Primary watermark text
        secondary_text: Secondary line (optional)
        width: Page width in points
        height: Page height in points
        opacity: Watermark opacity (0.0-1.0)
        angle: Rotation angle in degrees
        color: Hex color for primary text
        font_size: Primary text font size
        secondary_font_size: Secondary text font size

    Returns:
        BytesIO object containing the watermark PDF
    """
    packet = BytesIO()
    c = canvas.Canvas(packet, pagesize=(width, height))

    # Set up transparency
    c.saveState()

    # Convert hex color to RGB
    r, g, b = hex_to_rgb(color)

    # Calculate center position
    center_x = width / 2
    center_y = height / 2

    # Move to center and rotate
    c.translate(center_x, center_y)
    c.rotate(angle)

    # Draw primary text
    c.setFillColor(Color(r, g, b, alpha=opacity))
    c.setFont("Helvetica-Bold", font_size)

    # Center the text
    text_width = c.stringWidth(text, "Helvetica-Bold", font_size)
    c.drawString(-text_width / 2, 0, text)

    # Draw secondary text if provided
    if secondary_text:
        c.setFillColor(Color(1, 1, 1, alpha=opacity))  # White
        c.setFont("Helvetica", secondary_font_size)
        sec_text_width = c.stringWidth(secondary_text, "Helvetica", secondary_font_size)
        c.drawString(-sec_text_width / 2, -font_size - 10, secondary_text)

    c.restoreState()
    c.save()

    packet.seek(0)
    return packet


def watermark_pdf(input_path, output_path, text, secondary_text="",
                  opacity=0.3, angle=45, color="#d4a760",
                  coverage=0.5, password=None):
    """
    Add watermarks to a PDF file.

    Args:
        input_path: Path to input PDF
        output_path: Path to output PDF
        text: Primary watermark text
        secondary_text: Secondary line (optional)
        opacity: Watermark opacity (0.0-1.0)
        angle: Rotation angle in degrees
        color: Hex color for primary text
        coverage: Fraction of pages to watermark (0.0-1.0)
        password: Optional password to encrypt output

    Returns:
        True if successful, False otherwise
    """
    try:
        # Read the input PDF
        reader = PdfReader(input_path)
        writer = PdfWriter()

        num_pages = len(reader.pages)

        # Determine which pages to watermark (random selection based on coverage)
        pages_to_watermark = set()
        if coverage >= 1.0:
            # Watermark all pages
            pages_to_watermark = set(range(num_pages))
        elif coverage > 0:
            # Random selection
            num_to_mark = max(1, int(num_pages * coverage))
            pages_to_watermark = set(random.sample(range(num_pages), min(num_to_mark, num_pages)))

        print(f"Processing {num_pages} pages, watermarking {len(pages_to_watermark)} pages...")

        for page_num in range(num_pages):
            page = reader.pages[page_num]

            if page_num in pages_to_watermark:
                # Get page dimensions
                media_box = page.mediabox
                width = float(media_box.width)
                height = float(media_box.height)

                # Create watermark for this page size
                watermark_pdf_bytes = create_watermark_pdf(
                    text=text,
                    secondary_text=secondary_text,
                    width=width,
                    height=height,
                    opacity=opacity,
                    angle=angle,
                    color=color
                )

                watermark_reader = PdfReader(watermark_pdf_bytes)
                watermark_page = watermark_reader.pages[0]

                # Merge watermark onto page
                page.merge_page(watermark_page)

            writer.add_page(page)

        # Add password protection if specified
        if password:
            writer.encrypt(password)

        # Write output file
        with open(output_path, 'wb') as output_file:
            writer.write(output_file)

        print(f"Successfully created: {output_path}")
        return True

    except Exception as e:
        print(f"Error: {str(e)}", file=sys.stderr)
        return False


def main():
    parser = argparse.ArgumentParser(
        description='Add watermarks to PDF files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
    %(prog)s --input doc.pdf --output doc_wm.pdf --text "CONFIDENTIAL"
    %(prog)s --input doc.pdf --output doc_wm.pdf --text "Member #001" --secondary "email@example.com"
    %(prog)s --input doc.pdf --output doc_wm.pdf --text "Text" --opacity 0.5 --coverage 1.0
    %(prog)s --input doc.pdf --output doc_wm.pdf --text "Text" --password "secret123"
'''
    )

    parser.add_argument('--input', '-i', required=True,
                        help='Input PDF file path')
    parser.add_argument('--output', '-o', required=True,
                        help='Output PDF file path')
    parser.add_argument('--text', '-t', required=True,
                        help='Primary watermark text')
    parser.add_argument('--secondary', '-s', default='',
                        help='Secondary watermark text (optional)')
    parser.add_argument('--opacity', type=float, default=0.3,
                        help='Watermark opacity 0.0-1.0 (default: 0.3)')
    parser.add_argument('--angle', type=int, default=45,
                        help='Rotation angle in degrees (default: 45)')
    parser.add_argument('--color', default='#d4a760',
                        help='Primary text color in hex (default: #d4a760 golden)')
    parser.add_argument('--coverage', type=float, default=0.5,
                        help='Fraction of pages to watermark 0.0-1.0 (default: 0.5)')
    parser.add_argument('--password', '-p', default=None,
                        help='Password to encrypt output PDF (optional)')

    args = parser.parse_args()

    # Validate input file exists
    if not os.path.isfile(args.input):
        print(f"Error: Input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    # Validate opacity range
    if not 0.0 <= args.opacity <= 1.0:
        print("Error: Opacity must be between 0.0 and 1.0", file=sys.stderr)
        sys.exit(1)

    # Validate coverage range
    if not 0.0 <= args.coverage <= 1.0:
        print("Error: Coverage must be between 0.0 and 1.0", file=sys.stderr)
        sys.exit(1)

    # Create output directory if needed
    output_dir = os.path.dirname(args.output)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # Run watermarking
    success = watermark_pdf(
        input_path=args.input,
        output_path=args.output,
        text=args.text,
        secondary_text=args.secondary,
        opacity=args.opacity,
        angle=args.angle,
        color=args.color,
        coverage=args.coverage,
        password=args.password
    )

    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
