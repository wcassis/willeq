#!/usr/bin/env python3
"""
Merge multiple SF2 SoundFont files into a single file.

TinySoundFont (tsf.h) only supports loading one SF2 at a time, so this script
combines multiple SoundFonts offline. Higher-priority files override presets
from lower-priority files when they share the same bank/preset number.

Usage:
    python3 scripts/merge_sf2.py -o output.sf2 base.sf2 override1.sf2 override2.sf2

Files are listed in priority order: later files override earlier ones.
For EQ, the typical invocation is:

    python3 scripts/merge_sf2.py -o data/merged.sf2 \
        /path/to/EverQuest/synthusr.sf2 \
        /path/to/EverQuest/synthus2.sf2

This takes synthusr (original EQ launch, full GM set) as the base and overlays
synthus2 (Kunark/Velious era, partial override with higher-quality orchestral
samples) on top.

SF2 Binary Format Reference:
    RIFF('sfbk'
        LIST('INFO' ...)
        LIST('sdta'
            smpl: raw 16-bit sample data
        )
        LIST('pdta'
            phdr: preset headers
            pbag: preset zones (bags)
            pmod: preset modulators
            pgen: preset generators
            inst: instrument headers
            ibag: instrument zones
            imod: instrument modulators
            igen: instrument generators
            shdr: sample headers
        )
    )
"""

import argparse
import struct
import sys
import os
from collections import OrderedDict


# ── SF2 chunk record sizes ──────────────────────────────────────────────────

PHDR_SIZE = 38   # preset header
PBAG_SIZE = 4    # preset bag
PMOD_SIZE = 10   # preset modulator
PGEN_SIZE = 4    # preset generator
INST_SIZE = 22   # instrument header
IBAG_SIZE = 4    # instrument bag
IMOD_SIZE = 10   # instrument modulator
IGEN_SIZE = 4    # instrument generator
SHDR_SIZE = 46   # sample header


# ── Low-level RIFF/SF2 parser ───────────────────────────────────────────────

def read_u16(data, off):
    return struct.unpack_from('<H', data, off)[0]

def read_u32(data, off):
    return struct.unpack_from('<I', data, off)[0]

def read_i16(data, off):
    return struct.unpack_from('<h', data, off)[0]

def write_u16(val):
    return struct.pack('<H', val)

def write_u32(val):
    return struct.pack('<I', val)

def write_i16(val):
    return struct.pack('<h', val)


class SF2File:
    """Parsed SF2 file with all chunks accessible."""

    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.data = f.read()
        self._parse()

    def _parse(self):
        data = self.data

        # Verify RIFF/sfbk header
        assert data[0:4] == b'RIFF', f"Not a RIFF file: {self.path}"
        assert data[8:12] == b'sfbk', f"Not an SF2 file: {self.path}"

        # Find all LIST chunks
        self.info_chunks = {}  # tag -> bytes
        self.sdta_smpl = b''   # raw sample data
        self.sdta_sm24 = None  # optional 24-bit extension

        # pdta sub-chunks (raw bytes for each)
        self.phdr = b''
        self.pbag = b''
        self.pmod = b''
        self.pgen = b''
        self.inst = b''
        self.ibag = b''
        self.imod = b''
        self.igen = b''
        self.shdr = b''

        pos = 12  # after RIFF header + 'sfbk'
        file_size = read_u32(data, 4) + 8

        while pos < file_size:
            chunk_id = data[pos:pos+4]
            chunk_size = read_u32(data, pos + 4)

            if chunk_id == b'LIST':
                list_type = data[pos+8:pos+12]
                list_end = pos + 8 + chunk_size
                sub_pos = pos + 12

                if list_type == b'INFO':
                    while sub_pos < list_end:
                        sub_id = data[sub_pos:sub_pos+4]
                        sub_size = read_u32(data, sub_pos + 4)
                        self.info_chunks[sub_id] = data[sub_pos+8:sub_pos+8+sub_size]
                        sub_pos += 8 + sub_size
                        if sub_size % 2: sub_pos += 1  # RIFF padding

                elif list_type == b'sdta':
                    while sub_pos < list_end:
                        sub_id = data[sub_pos:sub_pos+4]
                        sub_size = read_u32(data, sub_pos + 4)
                        if sub_id == b'smpl':
                            self.sdta_smpl = data[sub_pos+8:sub_pos+8+sub_size]
                        elif sub_id == b'sm24':
                            self.sdta_sm24 = data[sub_pos+8:sub_pos+8+sub_size]
                        sub_pos += 8 + sub_size
                        if sub_size % 2: sub_pos += 1

                elif list_type == b'pdta':
                    while sub_pos < list_end:
                        sub_id = data[sub_pos:sub_pos+4]
                        sub_size = read_u32(data, sub_pos + 4)
                        chunk_data = data[sub_pos+8:sub_pos+8+sub_size]
                        tag = sub_id.decode('ascii')
                        if hasattr(self, tag):
                            setattr(self, tag, chunk_data)
                        sub_pos += 8 + sub_size
                        if sub_size % 2: sub_pos += 1

            pos += 8 + chunk_size
            if chunk_size % 2: pos += 1  # RIFF padding

    def get_preset_headers(self):
        """Return list of (bank, preset, name, bag_idx) tuples."""
        presets = []
        n = len(self.phdr) // PHDR_SIZE
        for i in range(n):
            off = i * PHDR_SIZE
            name = self.phdr[off:off+20].split(b'\x00')[0].decode('ascii', errors='replace')
            preset_num = read_u16(self.phdr, off + 20)
            bank = read_u16(self.phdr, off + 22)
            bag_idx = read_u16(self.phdr, off + 24)
            presets.append((bank, preset_num, name.strip(), bag_idx))
        return presets

    def get_instrument_headers(self):
        """Return list of (name, bag_idx) tuples."""
        instruments = []
        n = len(self.inst) // INST_SIZE
        for i in range(n):
            off = i * INST_SIZE
            name = self.inst[off:off+20].split(b'\x00')[0].decode('ascii', errors='replace')
            bag_idx = read_u16(self.inst, off + 20)
            instruments.append((name.strip(), bag_idx))
        return instruments

    def get_sample_headers(self):
        """Return list of sample header dicts."""
        samples = []
        n = len(self.shdr) // SHDR_SIZE
        for i in range(n):
            off = i * SHDR_SIZE
            name = self.shdr[off:off+20].split(b'\x00')[0].decode('ascii', errors='replace')
            start = read_u32(self.shdr, off + 20)
            end = read_u32(self.shdr, off + 24)
            loop_start = read_u32(self.shdr, off + 28)
            loop_end = read_u32(self.shdr, off + 32)
            sample_rate = read_u32(self.shdr, off + 36)
            original_pitch = self.shdr[off + 40]
            pitch_correction = struct.unpack_from('b', self.shdr, off + 41)[0]
            sample_link = read_u16(self.shdr, off + 42)
            sample_type = read_u16(self.shdr, off + 44)
            samples.append({
                'name': name.strip(),
                'start': start, 'end': end,
                'loop_start': loop_start, 'loop_end': loop_end,
                'sample_rate': sample_rate,
                'original_pitch': original_pitch,
                'pitch_correction': pitch_correction,
                'sample_link': sample_link,
                'sample_type': sample_type,
            })
        return samples


# ── Preset extraction ────────────────────────────────────────────────────────

class PresetData:
    """All data needed to reconstruct a single preset in the merged SF2."""

    def __init__(self, bank, preset_num, name):
        self.bank = bank
        self.preset_num = preset_num
        self.name = name
        self.pbags = []       # list of (pmod_records, pgen_records)
        self.instruments = []  # list of InstrumentData
        self.samples = []      # list of SampleData


class InstrumentData:
    def __init__(self, name):
        self.name = name
        self.ibags = []       # list of (imod_records, igen_records)


class SampleData:
    def __init__(self, header_dict, pcm_bytes):
        self.header = header_dict
        self.pcm = pcm_bytes


def extract_preset(sf2, preset_idx):
    """Extract all data for a single preset (by index in phdr)."""
    phdrs = sf2.get_preset_headers()
    ihdrs = sf2.get_instrument_headers()
    shdrs = sf2.get_sample_headers()

    bank, preset_num, name, bag_start = phdrs[preset_idx]

    # EOP sentinel check
    if name == 'EOP':
        return None

    pd = PresetData(bank, preset_num, name)

    # Preset bag range
    if preset_idx + 1 < len(phdrs):
        bag_end = phdrs[preset_idx + 1][3]
    else:
        bag_end = len(sf2.pbag) // PBAG_SIZE

    # Track which instruments and samples are referenced
    inst_indices = set()
    sample_indices = set()

    # Extract preset bags
    for bi in range(bag_start, bag_end):
        pbag_off = bi * PBAG_SIZE
        gen_idx = read_u16(sf2.pbag, pbag_off)
        mod_idx = read_u16(sf2.pbag, pbag_off + 2)

        # Generator range
        if bi + 1 < len(sf2.pbag) // PBAG_SIZE:
            next_off = (bi + 1) * PBAG_SIZE
            gen_end = read_u16(sf2.pbag, next_off)
            mod_end = read_u16(sf2.pbag, next_off + 2)
        else:
            gen_end = len(sf2.pgen) // PGEN_SIZE
            mod_end = len(sf2.pmod) // PMOD_SIZE

        pmods = sf2.pmod[mod_idx * PMOD_SIZE : mod_end * PMOD_SIZE]
        pgens = sf2.pgen[gen_idx * PGEN_SIZE : gen_end * PGEN_SIZE]

        # Find instrument references in generators (genOper == 41 is instrument)
        for gi in range(gen_idx, gen_end):
            goff = gi * PGEN_SIZE
            oper = read_u16(sf2.pgen, goff)
            if oper == 41:  # instrument
                inst_idx = read_u16(sf2.pgen, goff + 2)
                inst_indices.add(inst_idx)

        pd.pbags.append((pmods, pgens))

    # Extract referenced instruments
    for inst_idx in sorted(inst_indices):
        if inst_idx >= len(ihdrs):
            continue
        inst_name, ibag_start = ihdrs[inst_idx]

        # Skip EOS sentinel
        if inst_name == 'EOI':
            continue

        inst_data = InstrumentData(inst_name)

        if inst_idx + 1 < len(ihdrs):
            ibag_end = ihdrs[inst_idx + 1][1]
        else:
            ibag_end = len(sf2.ibag) // IBAG_SIZE

        for ibi in range(ibag_start, ibag_end):
            ibag_off = ibi * IBAG_SIZE
            igen_idx = read_u16(sf2.ibag, ibag_off)
            imod_idx = read_u16(sf2.ibag, ibag_off + 2)

            if ibi + 1 < len(sf2.ibag) // IBAG_SIZE:
                next_off = (ibi + 1) * IBAG_SIZE
                igen_end = read_u16(sf2.ibag, next_off)
                imod_end = read_u16(sf2.ibag, next_off + 2)
            else:
                igen_end = len(sf2.igen) // IGEN_SIZE
                imod_end = len(sf2.imod) // IMOD_SIZE

            imods = sf2.imod[imod_idx * IMOD_SIZE : imod_end * IMOD_SIZE]
            igens = sf2.igen[igen_idx * IGEN_SIZE : igen_end * IGEN_SIZE]

            # Find sample references (genOper == 53 is sampleID)
            for gi in range(igen_idx, igen_end):
                goff = gi * IGEN_SIZE
                oper = read_u16(sf2.igen, goff)
                if oper == 53:  # sampleID
                    sample_idx = read_u16(sf2.igen, goff + 2)
                    sample_indices.add(sample_idx)

            inst_data.ibags.append((imods, igens))

        pd.instruments.append(inst_data)

    # Extract referenced samples
    for si in sorted(sample_indices):
        if si >= len(shdrs):
            continue
        shdr = shdrs[si]
        if shdr['name'] == 'EOS':
            continue

        # Extract PCM data (16-bit samples, offsets are in sample points)
        pcm_start = shdr['start'] * 2  # 16-bit = 2 bytes per sample
        pcm_end = shdr['end'] * 2
        pcm = sf2.sdta_smpl[pcm_start:pcm_end]

        pd.samples.append(SampleData(shdr, pcm))

    return pd


# ── SF2 builder ──────────────────────────────────────────────────────────────

class SF2Builder:
    """Builds a new SF2 file from extracted preset data."""

    def __init__(self, name="Merged SoundFont"):
        self.name = name
        self.smpl_data = bytearray()  # accumulated sample PCM
        self.sample_offset = 0        # current offset in sample points

        # pdta accumulators
        self.phdr_records = []
        self.pbag_records = []
        self.pmod_data = bytearray()
        self.pgen_data = bytearray()
        self.inst_records = []
        self.ibag_records = []
        self.imod_data = bytearray()
        self.igen_data = bytearray()
        self.shdr_records = []

        # Dedup maps
        self._sample_map = {}  # (source_path, orig_sample_idx) -> new_sample_idx
        self._inst_map = {}    # (source_path, orig_inst_name, orig_inst_idx) -> new_inst_idx

    def add_preset(self, sf2, preset_idx):
        """Add a preset from a parsed SF2 file."""
        phdrs = sf2.get_preset_headers()
        ihdrs = sf2.get_instrument_headers()
        shdrs = sf2.get_sample_headers()

        bank, preset_num, name, bag_start = phdrs[preset_idx]
        if name == 'EOP':
            return

        # Preset bag range
        if preset_idx + 1 < len(phdrs):
            bag_end = phdrs[preset_idx + 1][3]
        else:
            bag_end = len(sf2.pbag) // PBAG_SIZE

        # Record preset header
        phdr_bag_start = len(self.pbag_records)
        self.phdr_records.append((name, preset_num, bank, phdr_bag_start))

        for bi in range(bag_start, bag_end):
            pbag_off = bi * PBAG_SIZE
            gen_idx = read_u16(sf2.pbag, pbag_off)
            mod_idx = read_u16(sf2.pbag, pbag_off + 2)

            if bi + 1 < len(sf2.pbag) // PBAG_SIZE:
                next_off = (bi + 1) * PBAG_SIZE
                gen_end = read_u16(sf2.pbag, next_off)
                mod_end = read_u16(sf2.pbag, next_off + 2)
            else:
                gen_end = len(sf2.pgen) // PGEN_SIZE
                mod_end = len(sf2.pmod) // PMOD_SIZE

            # Copy modulators
            pmod_start = len(self.pmod_data) // PMOD_SIZE
            self.pmod_data.extend(sf2.pmod[mod_idx * PMOD_SIZE : mod_end * PMOD_SIZE])

            # Copy generators, remapping instrument references
            pgen_start = len(self.pgen_data) // PGEN_SIZE
            for gi in range(gen_idx, gen_end):
                goff = gi * PGEN_SIZE
                oper = read_u16(sf2.pgen, goff)
                amount = sf2.pgen[goff + 2 : goff + 4]

                if oper == 41:  # instrument reference
                    orig_inst_idx = read_u16(sf2.pgen, goff + 2)
                    new_inst_idx = self._ensure_instrument(sf2, orig_inst_idx)
                    self.pgen_data.extend(write_u16(oper))
                    self.pgen_data.extend(write_u16(new_inst_idx))
                else:
                    self.pgen_data.extend(sf2.pgen[goff:goff + PGEN_SIZE])

            self.pbag_records.append((pgen_start, pmod_start))

    def _ensure_instrument(self, sf2, inst_idx):
        """Add an instrument if not already added. Returns new index."""
        key = (sf2.path, inst_idx)
        if key in self._inst_map:
            return self._inst_map[key]

        ihdrs = sf2.get_instrument_headers()
        if inst_idx >= len(ihdrs):
            return 0

        inst_name, ibag_start = ihdrs[inst_idx]
        new_inst_idx = len(self.inst_records)
        self._inst_map[key] = new_inst_idx

        if inst_idx + 1 < len(ihdrs):
            ibag_end = ihdrs[inst_idx + 1][1]
        else:
            ibag_end = len(sf2.ibag) // IBAG_SIZE

        inst_ibag_start = len(self.ibag_records)
        self.inst_records.append((inst_name, inst_ibag_start))

        for ibi in range(ibag_start, ibag_end):
            ibag_off = ibi * IBAG_SIZE
            igen_idx = read_u16(sf2.ibag, ibag_off)
            imod_idx = read_u16(sf2.ibag, ibag_off + 2)

            if ibi + 1 < len(sf2.ibag) // IBAG_SIZE:
                next_off = (ibi + 1) * IBAG_SIZE
                igen_end = read_u16(sf2.ibag, next_off)
                imod_end = read_u16(sf2.ibag, next_off + 2)
            else:
                igen_end = len(sf2.igen) // IGEN_SIZE
                imod_end = len(sf2.imod) // IMOD_SIZE

            # Copy modulators
            imod_start = len(self.imod_data) // IMOD_SIZE
            self.imod_data.extend(sf2.imod[imod_idx * IMOD_SIZE : imod_end * IMOD_SIZE])

            # Copy generators, remapping sample references
            igen_start = len(self.igen_data) // IGEN_SIZE
            for gi in range(igen_idx, igen_end):
                goff = gi * IGEN_SIZE
                oper = read_u16(sf2.igen, goff)

                if oper == 53:  # sampleID
                    orig_sample_idx = read_u16(sf2.igen, goff + 2)
                    new_sample_idx = self._ensure_sample(sf2, orig_sample_idx)
                    self.igen_data.extend(write_u16(oper))
                    self.igen_data.extend(write_u16(new_sample_idx))
                else:
                    self.igen_data.extend(sf2.igen[goff:goff + IGEN_SIZE])

            self.ibag_records.append((igen_start, imod_start))

        return new_inst_idx

    def _ensure_sample(self, sf2, sample_idx):
        """Add a sample if not already added. Returns new index."""
        key = (sf2.path, sample_idx)
        if key in self._sample_map:
            return self._sample_map[key]

        shdrs = sf2.get_sample_headers()
        if sample_idx >= len(shdrs):
            return 0

        shdr = shdrs[sample_idx]
        new_idx = len(self.shdr_records)
        self._sample_map[key] = new_idx

        # Copy PCM data
        pcm_start = shdr['start'] * 2
        pcm_end = shdr['end'] * 2
        pcm = sf2.sdta_smpl[pcm_start:pcm_end]

        # Calculate new offsets (in sample points)
        new_start = self.sample_offset
        sample_count = (shdr['end'] - shdr['start'])
        new_end = new_start + sample_count

        # Loop offsets are relative to sample start in original
        loop_start_rel = shdr['loop_start'] - shdr['start']
        loop_end_rel = shdr['loop_end'] - shdr['start']
        new_loop_start = new_start + loop_start_rel
        new_loop_end = new_start + loop_end_rel

        self.smpl_data.extend(pcm)
        # SF2 spec requires 46 zero samples after each sample for interpolation
        self.smpl_data.extend(b'\x00' * 92)  # 46 samples * 2 bytes
        self.sample_offset = new_end + 46

        # Handle linked samples (stereo pairs)
        sample_link = shdr['sample_link']
        new_link = 0
        if shdr['sample_type'] in (2, 4) and sample_link < len(shdrs):
            # Linked sample - ensure it's also added
            link_key = (sf2.path, sample_link)
            if link_key in self._sample_map:
                new_link = self._sample_map[link_key]
            else:
                # Will be resolved in a second pass or add it now
                new_link = self._ensure_sample(sf2, sample_link)

        self.shdr_records.append({
            'name': shdr['name'],
            'start': new_start,
            'end': new_end,
            'loop_start': new_loop_start,
            'loop_end': new_loop_end,
            'sample_rate': shdr['sample_rate'],
            'original_pitch': shdr['original_pitch'],
            'pitch_correction': shdr['pitch_correction'],
            'sample_link': new_link,
            'sample_type': shdr['sample_type'],
        })

        return new_idx

    def build(self):
        """Generate the complete SF2 file as bytes."""
        # Add sentinel records (EOP, EOI, EOS)
        self._add_sentinels()

        # Build pdta sub-chunks
        phdr_bytes = self._build_phdr()
        pbag_bytes = self._build_pbag()
        pmod_bytes = bytes(self.pmod_data) if self.pmod_data else b'\x00' * PMOD_SIZE
        pgen_bytes = bytes(self.pgen_data) if self.pgen_data else b'\x00' * PGEN_SIZE
        inst_bytes = self._build_inst()
        ibag_bytes = self._build_ibag()
        imod_bytes = bytes(self.imod_data) if self.imod_data else b'\x00' * IMOD_SIZE
        igen_bytes = bytes(self.igen_data) if self.igen_data else b'\x00' * IGEN_SIZE
        shdr_bytes = self._build_shdr()

        # Ensure modulator chunks have at least the terminal record
        if len(pmod_bytes) < PMOD_SIZE:
            pmod_bytes = b'\x00' * PMOD_SIZE
        if len(imod_bytes) < IMOD_SIZE:
            imod_bytes = b'\x00' * IMOD_SIZE

        # Build INFO list
        info_data = bytearray()
        ifil = struct.pack('<HH', 2, 1)  # SF2 version 2.1
        info_data.extend(self._make_chunk(b'ifil', ifil))
        isng = b'EMU8000\x00'
        info_data.extend(self._make_chunk(b'isng', isng))
        inam = self.name.encode('ascii') + b'\x00'
        if len(inam) % 2: inam += b'\x00'
        info_data.extend(self._make_chunk(b'INAM', inam))

        info_list = self._make_list(b'INFO', info_data)

        # Build sdta list
        smpl_chunk = self._make_chunk(b'smpl', bytes(self.smpl_data))
        sdta_list = self._make_list(b'sdta', smpl_chunk)

        # Build pdta list
        pdta_data = bytearray()
        pdta_data.extend(self._make_chunk(b'phdr', phdr_bytes))
        pdta_data.extend(self._make_chunk(b'pbag', pbag_bytes))
        pdta_data.extend(self._make_chunk(b'pmod', pmod_bytes))
        pdta_data.extend(self._make_chunk(b'pgen', pgen_bytes))
        pdta_data.extend(self._make_chunk(b'inst', inst_bytes))
        pdta_data.extend(self._make_chunk(b'ibag', ibag_bytes))
        pdta_data.extend(self._make_chunk(b'imod', imod_bytes))
        pdta_data.extend(self._make_chunk(b'igen', igen_bytes))
        pdta_data.extend(self._make_chunk(b'shdr', shdr_bytes))
        pdta_list = self._make_list(b'pdta', pdta_data)

        # Build RIFF
        riff_data = info_list + sdta_list + pdta_list
        result = bytearray()
        result.extend(b'RIFF')
        result.extend(write_u32(len(riff_data) + 4))  # +4 for 'sfbk'
        result.extend(b'sfbk')
        result.extend(riff_data)

        return bytes(result)

    def _add_sentinels(self):
        """Add required EOP/EOI/EOS sentinel records."""
        # EOP (end of presets)
        self.phdr_records.append(('EOP', 0, 0, len(self.pbag_records)))

        # Terminal pbag
        pgen_end = len(self.pgen_data) // PGEN_SIZE
        pmod_end = len(self.pmod_data) // PMOD_SIZE
        self.pbag_records.append((pgen_end, pmod_end))

        # EOI (end of instruments)
        self.inst_records.append(('EOI', len(self.ibag_records)))

        # Terminal ibag
        igen_end = len(self.igen_data) // IGEN_SIZE
        imod_end = len(self.imod_data) // IMOD_SIZE
        self.ibag_records.append((igen_end, imod_end))

        # EOS (end of samples)
        self.shdr_records.append({
            'name': 'EOS', 'start': 0, 'end': 0,
            'loop_start': 0, 'loop_end': 0,
            'sample_rate': 0, 'original_pitch': 0,
            'pitch_correction': 0, 'sample_link': 0,
            'sample_type': 0,
        })

    def _build_phdr(self):
        data = bytearray()
        for name, preset, bank, bag_idx in self.phdr_records:
            name_bytes = name.encode('ascii', errors='replace')[:19].ljust(20, b'\x00')
            data.extend(name_bytes)
            data.extend(write_u16(preset))
            data.extend(write_u16(bank))
            data.extend(write_u16(bag_idx))
            data.extend(b'\x00' * 12)  # library, genre, morphology
        return bytes(data)

    def _build_pbag(self):
        data = bytearray()
        for gen_idx, mod_idx in self.pbag_records:
            data.extend(write_u16(gen_idx))
            data.extend(write_u16(mod_idx))
        return bytes(data)

    def _build_inst(self):
        data = bytearray()
        for name, bag_idx in self.inst_records:
            name_bytes = name.encode('ascii', errors='replace')[:19].ljust(20, b'\x00')
            data.extend(name_bytes)
            data.extend(write_u16(bag_idx))
        return bytes(data)

    def _build_ibag(self):
        data = bytearray()
        for gen_idx, mod_idx in self.ibag_records:
            data.extend(write_u16(gen_idx))
            data.extend(write_u16(mod_idx))
        return bytes(data)

    def _build_shdr(self):
        data = bytearray()
        for s in self.shdr_records:
            name_bytes = s['name'].encode('ascii', errors='replace')[:19].ljust(20, b'\x00')
            data.extend(name_bytes)
            data.extend(write_u32(s['start']))
            data.extend(write_u32(s['end']))
            data.extend(write_u32(s['loop_start']))
            data.extend(write_u32(s['loop_end']))
            data.extend(write_u32(s['sample_rate']))
            data.extend(struct.pack('B', s['original_pitch']))
            data.extend(struct.pack('b', s['pitch_correction']))
            data.extend(write_u16(s['sample_link']))
            data.extend(write_u16(s['sample_type']))
        return bytes(data)

    @staticmethod
    def _make_chunk(tag, data):
        chunk = bytearray()
        chunk.extend(tag)
        chunk.extend(write_u32(len(data)))
        chunk.extend(data)
        if len(data) % 2:
            chunk.extend(b'\x00')  # RIFF padding
        return chunk

    @staticmethod
    def _make_list(list_type, data):
        result = bytearray()
        result.extend(b'LIST')
        result.extend(write_u32(len(data) + 4))  # +4 for list_type
        result.extend(list_type)
        result.extend(data)
        return result


# ── Main merge logic ─────────────────────────────────────────────────────────

def merge_sf2_files(input_paths, output_path, verbose=False):
    """
    Merge SF2 files. Later files override earlier ones for matching bank/preset.
    """
    # Parse all input files
    sf2_files = []
    for path in input_paths:
        if verbose:
            print(f"Loading: {path}")
        sf2_files.append(SF2File(path))

    # Build preset map: (bank, preset) -> (sf2_index, preset_index)
    # Later files override earlier ones
    preset_map = OrderedDict()
    for fi, sf2 in enumerate(sf2_files):
        phdrs = sf2.get_preset_headers()
        for pi, (bank, preset_num, name, _) in enumerate(phdrs):
            if name == 'EOP':
                continue
            key = (bank, preset_num)
            if key in preset_map and verbose:
                old_fi, old_pi = preset_map[key]
                old_name = sf2_files[old_fi].get_preset_headers()[old_pi][2]
                print(f"  Override: Bank {bank} Preset {preset_num}: "
                      f"'{old_name}' ({os.path.basename(input_paths[old_fi])}) "
                      f"-> '{name}' ({os.path.basename(input_paths[fi])})")
            preset_map[key] = (fi, pi)

    # Sort by bank/preset
    sorted_keys = sorted(preset_map.keys())

    if verbose:
        print(f"\nMerged preset count: {len(sorted_keys)}")

    # Build output
    builder = SF2Builder("WillEQ Merged SoundFont")

    for key in sorted_keys:
        fi, pi = preset_map[key]
        sf2 = sf2_files[fi]
        builder.add_preset(sf2, pi)
        if verbose:
            bank, preset_num, name, _ = sf2.get_preset_headers()[pi]
            src = os.path.basename(input_paths[fi])
            print(f"  Bank {bank:3d} Preset {preset_num:3d}: {name:20s} <- {src}")

    # Write output
    output_data = builder.build()
    with open(output_path, 'wb') as f:
        f.write(output_data)

    if verbose:
        print(f"\nWrote: {output_path} ({len(output_data) / 1024 / 1024:.1f} MB)")
        print(f"  Presets: {len(sorted_keys)}")
        print(f"  Samples: {len(builder.shdr_records) - 1}")  # -1 for EOS


def main():
    parser = argparse.ArgumentParser(
        description="Merge multiple SF2 SoundFont files into one. "
                    "Later files override earlier ones for matching bank/preset numbers.",
        epilog="Example:\n"
               "  %(prog)s -o data/merged.sf2 synthusr.sf2 synthus2.sf2\n\n"
               "This takes synthusr as the base GM set and overlays synthus2's\n"
               "higher-quality orchestral patches on top.",
        formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument('inputs', nargs='+', help='Input SF2 files (priority: last wins)')
    parser.add_argument('-o', '--output', required=True, help='Output SF2 file path')
    parser.add_argument('-v', '--verbose', action='store_true', help='Print detailed merge info')
    parser.add_argument('--list', action='store_true',
                        help='List presets in input files and exit (no merge)')

    args = parser.parse_args()

    # Validate inputs
    for path in args.inputs:
        if not os.path.exists(path):
            print(f"Error: File not found: {path}", file=sys.stderr)
            sys.exit(1)

    if args.list:
        for path in args.inputs:
            sf2 = SF2File(path)
            phdrs = sf2.get_preset_headers()
            shdrs = sf2.get_sample_headers()
            print(f"=== {os.path.basename(path)} ({os.path.getsize(path) / 1024 / 1024:.1f} MB) ===")
            print(f"  Presets: {len([p for p in phdrs if p[2] != 'EOP'])}, "
                  f"Samples: {len([s for s in shdrs if s['name'] != 'EOS'])}")
            for bank, preset, name, _ in phdrs:
                if name == 'EOP':
                    continue
                print(f"    Bank {bank:3d} Preset {preset:3d}: {name}")
            print()
        sys.exit(0)

    merge_sf2_files(args.inputs, args.output, verbose=args.verbose)


if __name__ == '__main__':
    main()
