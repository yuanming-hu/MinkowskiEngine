# Copyright (c) Chris Choy (chrischoy@ai.stanford.edu).
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
# of the Software, and to permit persons to whom the Software is furnished to do
# so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# Please cite "4D Spatio-Temporal ConvNets: Minkowski Convolutional Neural
# Networks", CVPR'19 (https://arxiv.org/abs/1904.08755) if you use any part
# of the code.
import numpy as np

import torch


def get_coords(data, batch_index=0):
    coords = []
    for i, row in enumerate(data):
        for j, col in enumerate(row):
            if col != ' ':
                coords.append([i, j, batch_index])
    return coords


def data_loader(in_feat_channel=3, max_label=5, is_classification=True):
    data = ["   X   ", "  X X  ", " XXXXX "]

    coords = get_coords(data, 0)
    coords.extend(get_coords(data, 1))
    coords = torch.from_numpy(np.array(coords)).int()
    N = len(coords)
    feats = torch.randn(N, in_feat_channel)
    label = (torch.rand(2 if is_classification else N) * max_label).long()
    return coords, feats, label
