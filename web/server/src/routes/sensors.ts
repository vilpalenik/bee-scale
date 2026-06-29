import { Router } from 'express';
import { postReading } from '../controllers/sensorController';

const router = Router();

router.post('/reading', postReading);

export default router;
