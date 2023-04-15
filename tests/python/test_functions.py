import numpy as np
import pytest
import proxnlp

from proxnlp import manifolds, residuals


class TestCompose:
    space = manifolds.SO2() * manifolds.SE3()
    fn = residuals.ManifoldDifferenceToPoint(space, space.rand())

    A = np.random.randn(3, fn.ndx)
    linear_op = residuals.LinearFunction(A)

    def test_compose(self):
        A = self.A
        linear_op = self.linear_op
        fn = self.fn
        space = self.space
        assert np.allclose(A, linear_op.A)
        assert np.allclose(0.0, linear_op.b)

        # use compose struct
        c1 = proxnlp.ComposeFunction(linear_op, fn)

        assert c1.ndx == fn.ndx
        assert c1.nr == A.shape[0]

        x0 = space.rand()
        s0 = c1(x0)

        assert np.allclose(s0, A @ fn(x0))

    def test_compose_fn(self):
        x0 = self.space.rand()
        A = self.A
        linear_op = self.linear_op
        # use compose function
        c2 = proxnlp.compose(linear_op, self.fn)
        assert c2.ndx == self.fn.ndx
        assert c2.nr == A.shape[0]

        s1 = c2(x0)
        assert np.allclose(s1, A @ self.fn(x0))

    def test_matmul(self):
        # use __matmul__ or arobase (@) operator
        c3 = self.linear_op @ self.fn
        assert c3.ndx == self.fn.ndx
        assert c3.nr == self.A.shape[0]

    def test_matmul_wrong_dim_exc(self):
        # test composing linear_op with itself (raises error)
        with pytest.raises(RuntimeError) as exc_info:
            _ = self.linear_op @ self.linear_op
        assert exc_info.type is RuntimeError
        assert "Incompatible dimensions" in exc_info.value.args[0]


if __name__ == "__main__":
    import sys

    sys.exit(pytest.main(sys.argv))