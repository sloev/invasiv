import os
import torch
import torch.nn as nn
import torch.nn.functional as F

# We will load the original weights into the original model, 
# then wrap it in an ONNX-friendly module.
from BeatNet.model import BDA

class BDA_ONNX(nn.Module):
    def __init__(self, original_model):
        super().__init__()
        self.dim_in = original_model.dim_in
        self.dim_hd = original_model.dim_hd
        self.num_layers = original_model.num_layers
        self.conv_out = original_model.conv_out
        
        self.conv1 = original_model.conv1
        self.linear0 = original_model.linear0
        self.lstm = original_model.lstm
        self.linear = original_model.linear

    def forward(self, data, hidden, cell):
        # data: (batch, time, dim_in)
        batch_size = data.size(0)
        time_steps = data.size(1)

        # 1D conv over the frequency axis for each frame independently
        x = data.view(-1, 1, self.dim_in) 
        x = self.conv1(x)
        x = F.relu(x)
        x = F.max_pool1d(x, 2)
        
        # Flatten and linear
        x = x.view(batch_size * time_steps, -1)
        x = self.linear0(x)
        
        # Reshape for LSTM
        x = x.view(batch_size, time_steps, self.conv_out)
        
        # LSTM step
        x, (new_hidden, new_cell) = self.lstm(x, (hidden, cell))
        
        # Linear output
        out = self.linear(x)
        
        # Transpose to (batch, classes, time)
        out = out.transpose(1, 2) 
        
        # Softmax over classes (dim 1 because shape is batch, classes, time)
        out = F.softmax(out, dim=1)
        
        return out, new_hidden, new_cell

def main():
    print("Loading original BeatNet weights...")
    device = 'cpu'
    original_model = BDA(272, 150, 2, device)
    
    # Locate the model weights in the venv
    import BeatNet
    script_dir = os.path.dirname(BeatNet.__file__)
    weights_path = os.path.join(script_dir, 'models/model_1_weights.pt')
    
    original_model.load_state_dict(torch.load(weights_path, map_location=device), strict=False)
    original_model.eval()
    
    print("Wrapping in ONNX-friendly module...")
    onnx_model = BDA_ONNX(original_model)
    onnx_model.eval()
    
    # We want to export for real-time tracking, so batch_size=1, time_steps=1
    batch_size = 1
    time_steps = 1
    dim_in = 272
    dim_hd = 150
    num_layers = 2
    
    dummy_input = torch.randn(batch_size, time_steps, dim_in)
    dummy_hidden = torch.zeros(num_layers, batch_size, dim_hd)
    dummy_cell = torch.zeros(num_layers, batch_size, dim_hd)
    
    onnx_file_path = "beatnet.onnx"
    
    print(f"Exporting to {onnx_file_path}...")
    torch.onnx.export(
        onnx_model,
        (dummy_input, dummy_hidden, dummy_cell),
        onnx_file_path,
        export_params=True,
        opset_version=14,
        do_constant_folding=True,
        input_names=['input', 'hidden_in', 'cell_in'],
        output_names=['output', 'hidden_out', 'cell_out'],
        dynamic_axes={
            'input': {0: 'batch_size', 1: 'time_steps'},
            'hidden_in': {1: 'batch_size'},
            'cell_in': {1: 'batch_size'},
            'output': {0: 'batch_size', 2: 'time_steps'},
            'hidden_out': {1: 'batch_size'},
            'cell_out': {1: 'batch_size'}
        }
    )
    print("Export complete!")

if __name__ == '__main__':
    main()
