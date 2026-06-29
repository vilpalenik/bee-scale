import cors from 'cors';
import express from 'express';
import sensorRoutes from './routes/sensors';

const app = express();
const PORT = process.env.PORT ?? 3000;

app.use(cors());
app.use(express.json());

app.use('/sensors', sensorRoutes);

app.get('/health', (_req, res) => {
  res.json({ success: true });
});

// GET — returns test string
app.get('/version', (_req, res) => {
  res.json({ success: true, version: '1.0.0' });
});

// POST — reads body, echoes it back
app.post('/echo', (req, res) => {
  res.json({ success: true, received: req.body });
});

app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});
