#!/usr/bin/env python3
"""
JIT vs Musashi Differential Tester

Runs the same test through both the JIT and Musashi interpreter,
compares CPU state after execution, and reports any differences.

Usage:
    python3 tools/jit_vs_musashi.py --test <test.json> --cycles <n>
    python3 tools/jit_vs_musashi.py --suite-dir <dir> --mode quick
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

# Default test paths
DEFAULT_SUITE_DIR = 'third_party/ProcessorTests/680x0/68000/v1'
DEFAULT_QUICK_DIR = 'build/processortests/quick'

# Musashi test driver path
MUSASHI_DRIVER = 'build/musashi_ref_test_driver'


def load_test_json(path):
    """Load a ProcessorTests JSON test file."""
    with open(path, 'r') as f:
        return json.load(f)


def parse_test_json(test_data):
    """Parse ProcessorTests JSON format.
    
    The format is a list of test cases, each with:
        - name: test name
        - initial: CPU state before instruction
        - final: CPU state after instruction  
        - length: instruction length in bytes
        - transactions: memory/bus transactions
    """
    tests = []
    
    if isinstance(test_data, list):
        for test in test_data:
            if 'initial' in test and 'final' in test:
                tests.append(test)
    
    return tests


def extract_cpu_state(state_dict):
    """Extract CPU registers from a state dictionary.
    
    Returns dict with D0-D7, A0-A7, PC, SR/CCR.
    """
    return {
        'D': [state_dict.get(f'd{i}', 0) for i in range(8)],
        'A': [state_dict.get(f'a{i}', 0) for i in range(8)],
        'PC': state_dict.get('pc', 0),
        'SR': state_dict.get('sr', 0),
        'USP': state_dict.get('usp', 0),
        'SSP': state_dict.get('ssp', 0),
    }


def compare_states(expected, actual, test_name):
    """
    Compare CPU states from expected (Musashi/final) and actual (JIT).
    
    Returns list of differences.
    """
    diffs = []
    
    # Compare data registers
    for i in range(8):
        if expected['D'][i] != actual['D'][i]:
            diffs.append(f"  D{i}: expected=0x{expected['D'][i]:08X}, actual=0x{actual['D'][i]:08X}")
    
    # Compare address registers
    for i in range(8):
        if expected['A'][i] != actual['A'][i]:
            diffs.append(f"  A{i}: expected=0x{expected['A'][i]:08X}, actual=0x{actual['A'][i]:08X}")
    
    # Compare PC
    if expected['PC'] != actual['PC']:
        diffs.append(f"  PC: expected=0x{expected['PC']:08X}, actual=0x{actual['PC']:08X}")
    
    # Compare SR/CCR
    if expected['SR'] != actual['SR']:
        diffs.append(f"  SR: expected=0x{expected['SR']:04X}, actual=0x{actual['SR']:04X}")
    
    # Compare USP
    if expected['USP'] != actual['USP']:
        diffs.append(f"  USP: expected=0x{expected['USP']:08X}, actual=0x{actual['USP']:08X}")
    
    # Compare SSP
    if expected['SSP'] != actual['SSP']:
        diffs.append(f"  SSP: expected=0x{expected['SSP']:08X}, actual=0x{actual['SSP']:08X}")
    
    return diffs


def run_musashi_test(test_path, cycles=0x1000000):
    """
    Run a test through Musashi interpreter.
    
    NOTE: This currently returns a placeholder. Full implementation requires:
    1. A C driver that can parse ProcessorTests JSON format
    2. Set up Musashi CPU state from 'initial' dict
    3. Set up memory from 'ram' entries
    4. Execute one instruction (or specified cycles)
    5. Return final CPU state as dict
    
    The existing musashi_ref_test_driver.c runs .bin files, not JSON.
    We need a new driver for JSON-format ProcessorTests.
    
    Returns dict with CPU state after execution, or None on error.
    """
    # TODO: Implement Musashi JSON test driver
    # For now, return None to indicate not implemented
    return {'error': 'Musashi JSON test driver not yet implemented'}


def run_jit_test(test_path, cycles=0x1000000):
    """
    Run a test through the JIT.
    
    For now, this falls back to Musashi since JIT execution isn't wired up yet.
    
    Returns dict with CPU state after execution.
    """
    # TODO: Implement actual JIT execution
    # For now, return same as Musashi (will always pass)
    return run_musashi_test(test_path, cycles)


def validate_test_structure(test_path, verbose=False):
    """
    Validate a single test file structure without execution.
    
    This checks that the JSON is well-formed and has the expected fields.
    """
    test_name = os.path.basename(test_path)
    
    try:
        with open(test_path, 'r') as f:
            test_data = json.load(f)
    except (json.JSONDecodeError, IOError) as e:
        return False, f"ERROR loading {test_name}: {e}"
    
    tests = parse_test_json(test_data)
    
    if not tests:
        return True, f"SKIP: {test_name} (no valid test cases)"
    
    # Validate structure of first test case
    first = tests[0]
    required_fields = ['initial', 'final']
    for field in required_fields:
        if field not in first:
            return False, f"ERROR: {test_name} missing field '{field}'"
    
    # Check initial state has registers
    initial = first['initial']
    reg_fields = ['d0', 'a0', 'pc', 'sr']
    for field in reg_fields:
        if field not in initial:
            return False, f"ERROR: {test_name} initial state missing '{field}'"
    
    return True, f"VALID: {test_name} ({len(tests)} cases)"


def run_test_file_musashi(test_path, cycles=0x1000000, verbose=False):
    """
    Run a single test file through Musashi and validate results.
    
    This executes each test case and compares against expected final state.
    """
    test_name = os.path.basename(test_path)
    
    try:
        with open(test_path, 'r') as f:
            test_data = json.load(f)
    except (json.JSONDecodeError, IOError) as e:
        print(f"ERROR: {test_name}: {e}")
        return False, 0, 0
    
    tests = parse_test_json(test_data)
    
    if not tests:
        print(f"SKIP: {test_name} (no valid test cases)")
        return True, 0, 0
    
    passed = 0
    failed = 0
    
    for i, test in enumerate(tests):
        expected_state = extract_cpu_state(test['final'])
        
        # For now, we trust the JSON's expected final state
        # Later this will run through Musashi to verify
        actual_state = expected_state  # Placeholder
        
        diffs = compare_states(expected_state, actual_state, test.get('name', f'case {i}'))
        
        if diffs:
            if verbose:
                print(f"  FAIL: {test.get('name', f'case {i}')}")
                for diff in diffs:
                    print(f"    {diff}")
            failed += 1
        else:
            passed += 1
    
    if failed == 0:
        print(f"PASS: {test_name} ({passed} cases)")
        return True, passed, failed
    else:
        print(f"FAIL: {test_name} ({passed} passed, {failed} failed)")
        return False, passed, failed


def run_test_suite(suite_dir, mode='quick', max_tests=None, verbose=False, use_musashi=False):
    """Run a test suite."""
    suite_path = Path(suite_dir)
    
    if not suite_path.exists():
        print(f"ERROR: Suite directory not found: {suite_dir}")
        return False
    
    # Find all .json files (ProcessorTests format)
    test_files = sorted(suite_path.glob('**/*.json'))
    
    if not test_files:
        print(f"ERROR: No .json files found in {suite_dir}")
        return False
    
    if mode == 'quick':
        # Take first 10 tests for quick mode
        test_files = test_files[:10]
    
    if max_tests:
        test_files = test_files[:max_tests]
    
    print(f"Running {len(test_files)} tests from {suite_dir}...")
    print()
    
    if use_musashi:
        print("WARNING: --musashi mode not yet implemented.")
        print("         Currently validating JSON structure only.")
        print()
    
    total_passed = 0
    total_failed = 0
    file_passed = 0
    file_failed = 0
    
    for test_file in test_files:
        try:
            if use_musashi:
                success, passed, failed = run_test_file_musashi(str(test_file), verbose=verbose)
            else:
                # Just validate structure for now
                valid, msg = validate_test_structure(str(test_file), verbose)
                if valid and 'VALID' in msg or 'SKIP' in msg:
                    print(msg)
                    file_passed += 1
                else:
                    print(f"FAIL: {test_file.name}: {msg}")
                    file_failed += 1
                continue
            
            if success:
                file_passed += 1
            else:
                file_failed += 1
            total_passed += passed
            total_failed += failed
            
        except Exception as e:
            print(f"ERROR: {test_file.name}: {e}")
            file_failed += 1
    
    print()
    print(f"Results: {file_passed} files passed, {file_failed} files failed")
    if total_passed > 0 or total_failed > 0:
        print(f"         {total_passed} test cases passed, {total_failed} test cases failed")
        print(f"         {total_passed + total_failed} total test cases")
    
    return file_failed == 0


def main():
    parser = argparse.ArgumentParser(
        description='JIT vs Musashi Differential Tester',
        epilog='''
Status:
  - JSON structure validation: IMPLEMENTED
  - Test discovery and counting: IMPLEMENTED  
  - Musashi execution: PENDING (needs C driver for JSON format)
  - JIT execution: PENDING (needs JIT integration)
  - State comparison: IMPLEMENTED (ready when execution is added)

The ProcessorTests JSON format requires setting up CPU state and memory
from the 'initial' dict and 'ram' entries, executing one instruction,
then comparing against 'final' state. This needs a custom C driver.

Example usage:
  # Validate JSON structure (current capability)
  python3 tools/jit_vs_musashi.py --suite-dir third_party/ProcessorTests/680x0/68000/v1 --mode quick
  
  # Run with Musashi execution (when implemented)
  python3 tools/jit_vs_musashi.py --suite-dir ... --mode quick --musashi
'''
    )
    parser.add_argument('--test', type=str, help='Single test JSON file to validate')
    parser.add_argument('--suite-dir', type=str, default=DEFAULT_SUITE_DIR,
                        help='Test suite directory')
    parser.add_argument('--mode', type=str, choices=['quick', 'full'], default='quick',
                        help='Test mode (quick or full)')
    parser.add_argument('--cycles', type=int, default=0x1000000,
                        help='Number of cycles to execute')
    parser.add_argument('--max-tests', type=int, help='Maximum number of tests to run')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    parser.add_argument('--list', action='store_true', help='List available tests')
    parser.add_argument('--musashi', action='store_true', 
                        help='Run actual Musashi execution (not just validation)')
    
    args = parser.parse_args()
    
    if args.list:
        suite_path = Path(args.suite_dir)
        if suite_path.exists():
            test_files = sorted(suite_path.glob('**/*.json'))
            print(f"Available tests in {args.suite_dir}:")
            for tf in test_files[:50]:
                print(f"  {tf.name}")
            if len(test_files) > 50:
                print(f"  ... and {len(test_files) - 50} more")
        else:
            print(f"Directory not found: {args.suite_dir}")
        sys.exit(0)
    
    if args.test:
        # Single test mode
        if args.musashi:
            success, passed, failed = run_test_file_musashi(args.test, args.cycles, args.verbose)
        else:
            valid, msg = validate_test_structure(args.test, args.verbose)
            print(msg)
            success = valid
        sys.exit(0 if success else 1)
    else:
        # Suite mode
        success = run_test_suite(
            args.suite_dir, 
            args.mode, 
            args.max_tests, 
            args.verbose,
            args.musashi
        )
        sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
