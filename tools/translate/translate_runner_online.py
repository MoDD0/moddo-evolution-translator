#!/usr/bin/env python3
"""
translate_runner_online.py
Online translation runner using deep-translator library.
Reads HTML/text from stdin and writes translated content to stdout as JSON.

Options:
  --target <lang>     target ISO 639-1 language code (default: en)
  --provider <name>   translation provider (google, mymemory, libre, etc.)
  --html | --text     hint whether input is HTML (default: text)
  --debug             enable debug logging to /tmp/translate_online_debug.log

Supported providers:
  - google: Google Translate (free, no API key)
  - mymemory: MyMemory Translator (free, no API key, 500 chars/request limit)
  - libre: LibreTranslate (free, no API key if using public instance)
  - microsoft: Microsoft Translator (requires API key)
  - deepl: DeepL Translator (requires API key)
"""

import argparse
import os
import sys
import json
from typing import Optional

# Debug logging support
DEBUG_MODE = False
DEBUG_LOG_FILE = "/tmp/translate_online_debug.log"

def debug_log(msg):
    """Log debug message if debug mode is enabled"""
    if DEBUG_MODE:
        try:
            with open(DEBUG_LOG_FILE, "a") as f:
                f.write(msg + "\n")
        except Exception:
            pass


def detect_language(text: str, is_html: bool = False) -> str:
    """
    Detect the source language of the text.

    Args:
        text: Text to detect language from
        is_html: Whether text is HTML

    Returns:
        ISO 639-1 language code or "auto"
    """
    try:
        from langdetect import detect
        from bs4 import BeautifulSoup

        # Extract text from HTML if needed
        if is_html:
            soup = BeautifulSoup(text, 'html.parser')
            text = soup.get_text()

        # Detect language
        lang_code = detect(text)
        debug_log(f"Detected language: {lang_code}")
        return lang_code
    except Exception as e:
        debug_log(f"Language detection failed: {e}")
        return "auto"


def translate_html_carefully(translator, html_content: str) -> str:
    """
    Translate HTML content while preserving structure.
    Uses BeautifulSoup to parse and translates text nodes inline
    during tree traversal (same proven approach as the Argos runner).

    Args:
        translator: deep-translator translator instance
        html_content: HTML content to translate

    Returns:
        Translated HTML with preserved structure
    """
    try:
        from bs4 import BeautifulSoup, NavigableString, Comment, Doctype
        import re

        # Use html.parser — best for email HTML with potentially malformed content
        soup = BeautifulSoup(html_content, 'html.parser')
        debug_log("Using parser: html.parser")

        def should_translate_text(text):
            """Check if text is worth translating"""
            if not text:
                return False
            cleaned = text.strip()
            if not cleaned or len(cleaned) < 2:
                return False
            # Skip numbers-only text
            if re.match(r'^[\d\s\W]+$', cleaned):
                return False
            # Skip URLs and email addresses
            if any(x in cleaned.lower() for x in ['http://', 'https://', 'mailto:', '@']):
                return False
            return True

        # Tags whose text content must never be translated
        SKIP_TAGS = frozenset({
            'style', 'script', 'code', 'pre', 'kbd', 'samp', 'var',
            'svg', 'math', 'textarea', 'noscript',
        })

        translated_count = 0

        def translate_element(element):
            """Recursively translate text nodes inline during traversal"""
            nonlocal translated_count
            if isinstance(element, NavigableString):
                # Skip comments, doctype, and other special strings
                if isinstance(element, (Comment, Doctype)):
                    return

                # Skip text inside non-translatable tags
                if element.parent and element.parent.name in SKIP_TAGS:
                    return

                text = str(element)
                if should_translate_text(text):
                    try:
                        stripped = text.strip()
                        leading_ws = text[:len(text) - len(text.lstrip())]
                        trailing_ws = text[len(text.rstrip()):]

                        translated = translator.translate(stripped)

                        if translated and translated != stripped:
                            element.replace_with(
                                NavigableString(leading_ws + translated + trailing_ws)
                            )
                            translated_count += 1
                            debug_log(f"Translated: '{stripped[:50]}' -> '{translated[:50]}'")
                        else:
                            debug_log(f"Skipped (no change): '{stripped[:50]}'")
                    except Exception as e:
                        debug_log(f"Failed to translate text node: {e}")
            elif hasattr(element, 'children'):
                # Use list() to safely iterate while modifying the tree
                for child in list(element.children):
                    translate_element(child)

        translate_element(soup)
        debug_log(f"Translated {translated_count} text nodes")

        return str(soup)
    except Exception as e:
        debug_log(f"HTML translation failed: {e}")
        import traceback
        debug_log(traceback.format_exc())
        # Fallback: translate as plain text
        try:
            return translator.translate(html_content)
        except Exception as e2:
            debug_log(f"Fallback translation also failed: {e2}")
            return html_content  # Return original if all else fails


def translate_online(
    text: str,
    target_lang: str,
    provider: str,
    is_html: bool = False,
    api_key: Optional[str] = None
) -> dict:
    """
    Translate text using the specified online provider via deep-translator.
    Implements retry logic for network issues and rate limiting.

    Args:
        text: Text to translate
        target_lang: Target language code
        provider: Provider name (google, mymemory, libre, etc.)
        is_html: Whether input is HTML
        api_key: Optional API key for providers that require it

    Returns:
        Dict with "translated" key containing translated text, and optional "error" key
    """
    import time

    try:
        from deep_translator import GoogleTranslator, MyMemoryTranslator, LibreTranslator
        from deep_translator.exceptions import (
            NotValidPayload,
            TranslationNotFound,
            RequestError,
            TooManyRequests,
        )
    except ImportError as e:
        error_msg = f"Required library not installed: {e}. Please run: pip install deep-translator"
        debug_log(f"Import error: {error_msg}")
        return {"error": error_msg, "translated": text}

    debug_log(f"Translating with provider: {provider}")
    debug_log(f"Target language: {target_lang}")
    debug_log(f"Is HTML: {is_html}")
    debug_log(f"Input length: {len(text)} chars")

    # Validate input
    if not text or not text.strip():
        debug_log("Empty input text")
        return {"translated": text}

    try:
        # Detect source language
        source_lang = detect_language(text, is_html)
        debug_log(f"Detected source language: {source_lang}")

        if source_lang == target_lang:
            debug_log("Source and target languages are the same, returning input")
            return {"translated": text}

        # Create appropriate translator instance based on provider
        translator = None

        if provider == "google":
            # Google Translate: Free, no API key, auto-detect supported
            translator = GoogleTranslator(source=source_lang, target=target_lang)
            debug_log("Using Google Translate (free, unlimited)")

        elif provider == "mymemory":
            # MyMemory: Free but limited (500 chars/request), requires email in API call
            # Note: MyMemory API has been known to be unstable
            try:
                translator = MyMemoryTranslator(source=source_lang, target=target_lang)
                debug_log("Using MyMemory Translator (free, 500 char limit)")
            except Exception as e:
                debug_log(f"MyMemory initialization error: {e}")
                raise ValueError(f"MyMemory translator not available: {e}")

        elif provider == "libre":
            # LibreTranslate: Multiple public instances, some require API key
            base_url = os.environ.get("LIBRE_TRANSLATE_URL", "https://libretranslate.de")
            debug_log(f"Using LibreTranslate instance: {base_url}")

            # Try with API key first, fallback to no key
            try:
                if api_key:
                    translator = LibreTranslator(source=source_lang, target=target_lang,
                                                base_url=base_url, api_key=api_key)
                else:
                    # Try without API key (some instances allow this)
                    translator = LibreTranslator(source=source_lang, target=target_lang,
                                                base_url=base_url)
                debug_log("LibreTranslate initialized successfully")
            except Exception as e:
                debug_log(f"LibreTranslate initialization error: {e}")
                if "api" in str(e).lower() or "key" in str(e).lower():
                    raise ValueError(
                        f"LibreTranslate requires API key for {base_url}. "
                        "Try a different instance URL via LIBRE_TRANSLATE_URL environment variable "
                        "(e.g., https://libretranslate.de or https://translate.argosopentech.com)"
                    )
                raise

        else:
            raise ValueError(f"Unsupported provider: {provider}. Supported: google, libre")

        debug_log(f"Translator created: {type(translator).__name__}")

        # Retry configuration
        max_retries = 3
        retry_delay = 1  # seconds

        for attempt in range(max_retries):
            try:
                # Translate based on content type
                if is_html:
                    translated = translate_html_carefully(translator, text)
                else:
                    # For plain text, handle provider-specific limits
                    if provider == "mymemory" and len(text) > 500:
                        # MyMemory has 500 char limit, split into sentences
                        debug_log("Splitting text for MyMemory (500 char limit)")
                        sentences = text.split('. ')
                        translated_sentences = []
                        for sentence in sentences:
                            if sentence.strip():
                                translated_sentences.append(translator.translate(sentence.strip()))
                                time.sleep(0.5)  # Rate limiting
                        translated = '. '.join(translated_sentences)
                    else:
                        translated = translator.translate(text)

                debug_log(f"Translation successful, output length: {len(translated)} chars")
                return {"translated": translated}

            except TooManyRequests as e:
                debug_log(f"Rate limit hit (attempt {attempt + 1}/{max_retries}): {e}")
                if attempt < max_retries - 1:
                    wait_time = retry_delay * (2 ** attempt)  # Exponential backoff
                    debug_log(f"Waiting {wait_time}s before retry...")
                    time.sleep(wait_time)
                else:
                    raise

            except RequestError as e:
                debug_log(f"Network error (attempt {attempt + 1}/{max_retries}): {e}")
                if attempt < max_retries - 1:
                    wait_time = retry_delay * (2 ** attempt)
                    debug_log(f"Waiting {wait_time}s before retry...")
                    time.sleep(wait_time)
                else:
                    raise

    except NotValidPayload as e:
        error_msg = f"Invalid input for translation: {e}"
        debug_log(f"Validation error: {error_msg}")
        return {"error": error_msg, "translated": text}

    except TranslationNotFound as e:
        error_msg = f"Translation not found: {e}"
        debug_log(f"Translation not found: {error_msg}")
        return {"error": error_msg, "translated": text}

    except ValueError as e:
        error_msg = str(e)
        debug_log(f"Configuration error: {error_msg}")
        return {"error": error_msg, "translated": text}

    except Exception as e:
        error_msg = f"Translation failed: {type(e).__name__}: {e}"
        debug_log(f"Unexpected error: {error_msg}")
        import traceback
        debug_log(traceback.format_exc())
        return {"error": error_msg, "translated": text}


def main():
    """Main entry point for the online translation runner"""
    global DEBUG_MODE

    parser = argparse.ArgumentParser(description="Online translation runner using deep-translator")
    parser.add_argument("--target", default="en", help="Target language code (ISO 639-1)")
    parser.add_argument("--provider", default="google", help="Translation provider (google, mymemory, libre)")
    parser.add_argument("--html", dest="is_html", action="store_true", help="Input is HTML")
    parser.add_argument("--text", dest="is_html", action="store_false", help="Input is plain text")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")
    parser.add_argument("--api-key", help="API key for providers that require it")
    parser.set_defaults(is_html=False)

    args = parser.parse_args()

    DEBUG_MODE = args.debug

    if DEBUG_MODE:
        debug_log("=" * 60)
        debug_log(f"Online translation runner started")
        debug_log(f"Provider: {args.provider}")
        debug_log(f"Target: {args.target}")
        debug_log(f"HTML mode: {args.is_html}")

    # Read input from stdin
    input_text = sys.stdin.read()

    if not input_text:
        print(json.dumps({"error": "No input provided", "translated": ""}))
        return 1

    # Perform translation
    result = translate_online(
        text=input_text,
        target_lang=args.target,
        provider=args.provider,
        is_html=args.is_html,
        api_key=args.api_key
    )

    # Output result as JSON
    print(json.dumps(result))

    if DEBUG_MODE:
        debug_log("Translation complete")
        debug_log("=" * 60)

    return 0


if __name__ == "__main__":
    sys.exit(main())
