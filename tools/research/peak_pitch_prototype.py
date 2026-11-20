"""Host-only fixed-ratio pitch experiment for pitch_down_prototype.mjs.

Translate the spectral region around each peak as a unit and apply its phase
correction to the entire region. See Laroche and Dolson (1999) in the research note.
"""
import sys

import numpy as np


def shift(input_samples, ratio, size, padding=1, hop=128):
    window = np.hanning(size + 1)[:-1]
    fft_size = size * padding
    margin = (fft_size - size) // 2
    bins = np.arange(fft_size // 2 + 1)
    expected_phase = 2 * np.pi * hop / fft_size * bins
    correction = np.zeros(len(bins))
    previous_phase = np.zeros(len(bins))
    previous_magnitude = np.zeros(len(bins))
    padded = np.pad(input_samples, (size - hop, size))
    signal = np.zeros(len(padded))
    weight = np.zeros(len(padded))

    for start in range(0, len(padded) - size + 1, hop):
        frame = padded[start:start + size] * window
        spectrum = np.fft.rfft(np.fft.ifftshift(np.pad(frame, (margin, margin))))
        magnitude = np.abs(spectrum)
        phase = np.angle(spectrum)
        peaks = np.flatnonzero(
            (magnitude[1:-1] > magnitude[:-2])
            & (magnitude[1:-1] >= magnitude[2:])
            & (magnitude[1:-1] > np.max(magnitude) * 0.001)
        ) + 1
        if len(peaks):
            # Region boundaries lie at the magnitude valley between adjacent peaks.
            boundaries = [np.argmin(magnitude[a:b + 1]) + a + 1 for a, b in zip(peaks[:-1], peaks[1:])]
            regions = peaks[np.searchsorted(boundaries, bins, side="right")]
            residual = (phase - previous_phase - expected_phase + np.pi) % (2 * np.pi) - np.pi
            advance = expected_phase + residual
            active = previous_magnitude > magnitude * 0.1
            correction = np.where(active, correction + (ratio - 1) * advance, 0.0)
            correction = (correction + np.pi) % (2 * np.pi) - np.pi

            # Translate the window shape around each peak without rescaling it.
            # The centred FFT lets neighbouring bins interpolate in complex form.
            destination = bins + (ratio - 1) * regions
            left = np.floor(destination).astype(int)
            fraction = destination - left
            values = spectrum * np.exp(1j * correction[regions])
            shifted = np.zeros(len(bins), dtype=complex)
            for positions, gains in ((left, 1 - fraction), (left + 1, fraction)):
                keep = (positions >= 0) & (positions < len(bins))
                np.add.at(shifted, positions[keep], values[keep] * gains[keep])
            block = np.fft.fftshift(np.fft.irfft(shifted, n=fft_size))[margin:margin + size]
            signal[start:start + size] += block * window
        else:
            correction.fill(0)
        previous_phase = phase
        previous_magnitude = magnitude
        weight[start:start + size] += window * window

    aligned = np.divide(signal, weight, out=np.zeros_like(signal), where=weight > 1e-7)
    # The first output hop from a frame can be emitted once its N input samples
    # are available. Include that causal delay and return input-length output.
    delayed = np.pad(aligned[size - hop:], (size, 0))
    return delayed[:len(input_samples)]


if __name__ == "__main__":
    # Float32 mono at 44.1 kHz, raw little-endian samples on stdin and stdout.
    factor, window_size, padding = float(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    samples = np.frombuffer(sys.stdin.buffer.read(), dtype="<f4")
    sys.stdout.buffer.write(shift(samples, factor, window_size, padding).astype("<f4").tobytes())
